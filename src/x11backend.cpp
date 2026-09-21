#include "x11backend.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>

#include <cstring>

namespace TiltBack {
namespace {

bool nameDenied(const QString &name, quint32 vendor, quint32 product, bool touchpad)
{
    if (name.contains(QLatin1String("XTEST"), Qt::CaseInsensitive))
        return true;
    if (name.contains(QLatin1String("Virtual core"), Qt::CaseInsensitive))
        return true;
    if (name.contains(QLatin1String("cros_ec"), Qt::CaseInsensitive))
        return true;
    if (name.contains(QLatin1String("keyboard"), Qt::CaseInsensitive)
        && !name.contains(QLatin1String("Touchscreen"), Qt::CaseInsensitive))
        return true;
    return isDenied(name, touchpad, vendor, product);
}

bool isEraserName(const QString &name)
{
    return name.contains(QLatin1String("eraser"), Qt::CaseInsensitive);
}

bool isStylusName(const QString &name)
{
    return name.contains(QLatin1String("Stylus"), Qt::CaseInsensitive) && !isEraserName(name);
}

int quartersFromKscreen(const QString &kscreen)
{
    const QString t = normalizeKscreen(kscreen);
    if (t == QLatin1String("left"))
        return 1;
    if (t == QLatin1String("inverted"))
        return 2;
    if (t == QLatin1String("right"))
        return 3;
    return 0;
}

int quartersFromResidual(int r)
{
    switch (r) {
    case 1:
        return 1;
    case 2:
        return 3;
    case 4:
    case 8:
        return 2;
    default:
        return 0;
    }
}

int wacomFromQuarters(int q)
{
    switch (q & 3) {
    case 1:
        return 2; // ccw / left
    case 2:
        return 3; // half
    case 3:
        return 1; // cw / right
    default:
        return 0;
    }
}

int quartersFromWacom(int w)
{
    switch (w) {
    case 1:
        return 3;
    case 2:
        return 1;
    case 3:
        return 2;
    default:
        return 0;
    }
}

int composeWacom(const QString &tKscreen, int r)
{
    return wacomFromQuarters((quartersFromKscreen(tKscreen) + quartersFromResidual(r)) & 3);
}

int classifyWacomResidual(int wacom, const QString &tKscreen)
{
    const int q = (quartersFromWacom(wacom) - quartersFromKscreen(tKscreen) + 4) & 3;
    switch (q) {
    case 1:
        return 1;
    case 2:
        return 8;
    case 3:
        return 2;
    default:
        return 0;
    }
}

QString xrandrRotateArg(const QString &kscreen)
{
    const QString t = normalizeKscreen(kscreen);
    if (t == QLatin1String("left"))
        return QStringLiteral("left");
    if (t == QLatin1String("right"))
        return QStringLiteral("right");
    if (t == QLatin1String("inverted"))
        return QStringLiteral("inverted");
    return QStringLiteral("normal");
}

QString kscreenFromRandr(Rotation rotation)
{
    const Rotation r = rotation & 0x0f;
    if (r == RR_Rotate_90)
        return QStringLiteral("left");
    if (r == RR_Rotate_180)
        return QStringLiteral("inverted");
    if (r == RR_Rotate_270)
        return QStringLiteral("right");
    if (r == RR_Rotate_0)
        return QStringLiteral("none");
    return {};
}

} // namespace

namespace {

Display *asDisplay(void *dpy)
{
    return static_cast<Display *>(dpy);
}

} // namespace

X11Backend::X11Backend(QObject *parent)
    : OrientationBackend(parent)
{
    m_dpy = XOpenDisplay(nullptr);
    if (!m_dpy)
        return;
    int errBase = 0;
    if (!XRRQueryExtension(asDisplay(m_dpy), &m_rrEventBase, &errBase)) {
        XCloseDisplay(asDisplay(m_dpy));
        m_dpy = nullptr;
        return;
    }
    int major = 2;
    int minor = 0;
    if (XIQueryVersion(asDisplay(m_dpy), &major, &minor) != Success) {
        XCloseDisplay(asDisplay(m_dpy));
        m_dpy = nullptr;
        return;
    }
}

X11Backend::~X11Backend()
{
    if (m_dpy)
        XCloseDisplay(asDisplay(m_dpy));
}

QString X11Backend::persistHow() const
{
    return QStringLiteral("home.json only; follow writes CTM(T)∘CTM(R)");
}

QString X11Backend::pictureSource(const OutputInfo &out) const
{
    if (out.name.isEmpty())
        return QStringLiteral("RandR getConfig (live) + DMI + DRM");
    return QStringLiteral("RandR (live, %1) + DMI + DRM").arg(out.name);
}

QString X11Backend::inputSource(const Digitizer &dev) const
{
    const XiDev found = findDevice(dev);
    if (found.hasWacomRotation)
        return QStringLiteral("Wacom Rotation (name + VID:PID; xi:%1 is not identity)")
            .arg(found.deviceId);
    if (dev.sysName.isEmpty())
        return QStringLiteral("XInput CTM (name + VID:PID)");
    return QStringLiteral("XInput CTM (name + VID:PID; xi:%1 is not identity)").arg(dev.sysName);
}

bool X11Backend::hasProperty(int deviceId, const char *name) const
{
    if (!m_dpy || deviceId < 0)
        return false;
    const Atom atom = XInternAtom(asDisplay(m_dpy), name, True);
    if (atom == None)
        return false;
    int nprops = 0;
    Atom *props = XIListProperties(asDisplay(m_dpy), deviceId, &nprops);
    if (!props)
        return false;
    bool found = false;
    for (int i = 0; i < nprops; ++i) {
        if (props[i] == atom) {
            found = true;
            break;
        }
    }
    XFree(props);
    return found;
}

bool X11Backend::readVidPid(int deviceId, quint32 *vendor, quint32 *product) const
{
    if (vendor)
        *vendor = 0;
    if (product)
        *product = 0;
    const Atom atom = XInternAtom(asDisplay(m_dpy), "Device Product ID", True);
    if (atom == None)
        return false;
    Atom type = None;
    int fmt = 0;
    unsigned long n = 0;
    unsigned long after = 0;
    unsigned char *data = nullptr;
    if (XIGetProperty(asDisplay(m_dpy), deviceId, atom, 0, 2, False, AnyPropertyType, &type, &fmt, &n, &after,
                      &data)
            != Success
        || !data)
        return false;
    bool ok = false;
    if (fmt == 32 && n >= 2) {
        const quint32 *packed = reinterpret_cast<const quint32 *>(data);
        const long *cells = reinterpret_cast<const long *>(data);
        quint32 vendorV = static_cast<quint32>(cells[0]);
        quint32 productV = static_cast<quint32>(cells[1]);
        // XIGetProperty often packs CARD32 tightly (8 bytes for two items).
        if (productV == 0 && packed[1] != 0)
            productV = packed[1];
        if (vendor)
            *vendor = vendorV;
        if (product)
            *product = productV;
        ok = true;
    }
    XFree(data);
    return ok;
}

bool X11Backend::readCtm(int deviceId, float *out) const
{
    const Atom atom = XInternAtom(asDisplay(m_dpy), "Coordinate Transformation Matrix", True);
    if (atom == None)
        return false;
    Atom type = None;
    int fmt = 0;
    unsigned long n = 0;
    unsigned long after = 0;
    unsigned char *data = nullptr;
    if (XIGetProperty(asDisplay(m_dpy), deviceId, atom, 0, 9, False, AnyPropertyType, &type, &fmt, &n, &after,
                      &data)
            != Success
        || !data)
        return false;
    bool ok = false;
    if (fmt == 32 && n >= 9) {
        const float *v = reinterpret_cast<const float *>(data);
        for (int i = 0; i < 9; ++i)
            out[i] = v[i];
        ok = true;
    }
    XFree(data);
    return ok;
}

bool X11Backend::writeCtm(int deviceId, const float *ctm)
{
    const Atom atom = XInternAtom(asDisplay(m_dpy), "Coordinate Transformation Matrix", False);
    const Atom floatAtom = XInternAtom(asDisplay(m_dpy), "FLOAT", False);
    if (atom == None || floatAtom == None)
        return false;
    float copy[9];
    memcpy(copy, ctm, sizeof(copy));
    XIChangeProperty(asDisplay(m_dpy), deviceId, atom, floatAtom, 32, PropModeReplace,
                     reinterpret_cast<unsigned char *>(copy), 9);
    XFlush(asDisplay(m_dpy));
    return true;
}

bool X11Backend::readWacomRotation(int deviceId, int *value) const
{
    const Atom atom = XInternAtom(asDisplay(m_dpy), "Wacom Rotation", True);
    if (atom == None)
        return false;
    Atom type = None;
    int fmt = 0;
    unsigned long n = 0;
    unsigned long after = 0;
    unsigned char *data = nullptr;
    if (XIGetProperty(asDisplay(m_dpy), deviceId, atom, 0, 1, False, AnyPropertyType, &type, &fmt, &n, &after,
                      &data)
            != Success
        || !data)
        return false;
    bool ok = false;
    if (n >= 1) {
        if (fmt == 8)
            *value = static_cast<int>(data[0]);
        else if (fmt == 32)
            *value = static_cast<int>(*reinterpret_cast<long *>(data));
        else
            *value = static_cast<int>(data[0]);
        ok = true;
    }
    XFree(data);
    return ok;
}

bool X11Backend::writeWacomRotation(int deviceId, int value)
{
    const Atom atom = XInternAtom(asDisplay(m_dpy), "Wacom Rotation", True);
    if (atom == None)
        return false;
    Atom type = None;
    int fmt = 0;
    unsigned long n = 0;
    unsigned long after = 0;
    unsigned char *probe = nullptr;
    if (XIGetProperty(asDisplay(m_dpy), deviceId, atom, 0, 1, False, AnyPropertyType, &type, &fmt, &n, &after,
                      &probe)
            != Success) {
        return false;
    }
    if (probe)
        XFree(probe);
    if (type == None)
        type = XA_INTEGER;
    if (fmt == 8) {
        unsigned char v = static_cast<unsigned char>(value);
        XIChangeProperty(asDisplay(m_dpy), deviceId, atom, type, 8, PropModeReplace, &v, 1);
    } else {
        long v = value;
        XIChangeProperty(asDisplay(m_dpy), deviceId, atom, type == None ? XA_INTEGER : type, 32,
                         PropModeReplace, reinterpret_cast<unsigned char *>(&v), 1);
    }
    XFlush(asDisplay(m_dpy));
    return true;
}

QVector<X11Backend::XiDev> X11Backend::scanDevices() const
{
    QVector<XiDev> out;
    if (!m_dpy)
        return out;
    int n = 0;
    XIDeviceInfo *list = XIQueryDevice(asDisplay(m_dpy), XIAllDevices, &n);
    if (!list)
        return out;
    for (int i = 0; i < n; ++i) {
        const XIDeviceInfo *info = &list[i];
        if (info->use == XIMasterPointer || info->use == XIMasterKeyboard
            || info->use == XISlaveKeyboard)
            continue;
        const QString name = QString::fromLocal8Bit(info->name ? info->name : "");
        bool touch = false;
        bool absolute = false;
        bool touchpad = false;
        for (int c = 0; c < info->num_classes; ++c) {
            const XIAnyClassInfo *cls = info->classes[c];
            if (!cls)
                continue;
            if (cls->type == XITouchClass)
                touch = true;
            if (cls->type == XIValuatorClass) {
                const auto *val = reinterpret_cast<const XIValuatorClassInfo *>(cls);
                if (val->mode == XIModeAbsolute)
                    absolute = true;
            }
        }
        if (name.contains(QLatin1String("Touchpad"), Qt::CaseInsensitive))
            touchpad = true;
        quint32 vendor = 0;
        quint32 product = 0;
        readVidPid(info->deviceid, &vendor, &product);
        if (nameDenied(name, vendor, product, touchpad))
            continue;
        const bool stylus = isStylusName(name);
        const bool eraser = isEraserName(name);
        if (!touch && !absolute && !stylus && !eraser)
            continue;
        XiDev d;
        d.deviceId = info->deviceid;
        d.touch = touch || name.contains(QLatin1String("Touchscreen"), Qt::CaseInsensitive)
            || name.contains(QLatin1String("Elan"), Qt::CaseInsensitive);
        d.stylus = stylus;
        d.eraser = eraser;
        d.hasWacomRotation = hasProperty(info->deviceid, "Wacom Rotation");
        d.digitizer.name = name;
        d.digitizer.vendor = vendor;
        d.digitizer.product = product;
        d.digitizer.sysName = QString::number(info->deviceid);
        d.digitizer.path = QStringLiteral("xi:%1").arg(info->deviceid);
        d.digitizer.ok = true;
        bool ok = false;
        d.digitizer.r = residualAt(info->deviceid, d.hasWacomRotation, &ok);
        if (!ok)
            d.digitizer.r = 0;
        out.append(d);
    }
    XIFreeDeviceInfo(list);
    return out;
}

X11Backend::XiDev X11Backend::findDevice(const Digitizer &identity) const
{
    const QVector<XiDev> all = scanDevices();
    if (identity.ok && !identity.name.isEmpty()) {
        for (const XiDev &d : all) {
            if (d.digitizer.name == identity.name && d.digitizer.vendor == identity.vendor
                && d.digitizer.product == identity.product)
                return d;
        }
    }
    return {};
}

OutputInfo X11Backend::peekOutput() const
{
    OutputInfo info;
    if (!m_dpy)
        return info;
    const Window root = DefaultRootWindow(asDisplay(m_dpy));
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(asDisplay(m_dpy), root);
    if (!res)
        res = XRRGetScreenResources(asDisplay(m_dpy), root);
    if (!res)
        return info;
    RROutput fallbackOut = None;
    RRCrtc fallbackCrtc = None;
    QString fallbackName;
    RROutput chosenOut = None;
    RRCrtc chosenCrtc = None;
    QString chosenName;
    for (int i = 0; i < res->noutput; ++i) {
        XRROutputInfo *oi = XRRGetOutputInfo(asDisplay(m_dpy), res, res->outputs[i]);
        if (!oi)
            continue;
        const QString name = QString::fromLocal8Bit(oi->name ? oi->name : "");
        const bool connected = oi->connection == RR_Connected && oi->crtc != None;
        if (connected) {
            if (fallbackOut == None) {
                fallbackOut = res->outputs[i];
                fallbackCrtc = oi->crtc;
                fallbackName = name;
            }
            if (isBuiltinOutput(name)) {
                chosenOut = res->outputs[i];
                chosenCrtc = oi->crtc;
                chosenName = name;
            }
        }
        XRRFreeOutputInfo(oi);
        if (chosenOut != None)
            break;
    }
    if (chosenOut == None) {
        chosenOut = fallbackOut;
        chosenCrtc = fallbackCrtc;
        chosenName = fallbackName;
    }
    if (chosenCrtc != None) {
        XRRCrtcInfo *ci = XRRGetCrtcInfo(asDisplay(m_dpy), res, chosenCrtc);
        if (ci) {
            info.name = chosenName;
            info.tKscreen = kscreenFromRandr(ci->rotation);
            info.tKwin = kwinNameFromKscreen(info.tKscreen);
            info.ok = !info.tKscreen.isEmpty();
            XRRFreeCrtcInfo(ci);
        }
    }
    XRRFreeScreenResources(res);
    return info;
}

bool X11Backend::setOutput(const QString &kscreen, QString *error)
{
    const QString want = normalizeKscreen(kscreen);
    if (want.isEmpty()) {
        if (error)
            *error = QStringLiteral("unknown transform %1").arg(kscreen);
        return false;
    }
    OutputInfo live = readOutput();
    if (live.name.isEmpty()) {
        if (error)
            *error = QStringLiteral("no builtin RandR output");
        return false;
    }
    const QVector<XiDev> before = scanDevices();
    QVector<int> residuals;
    residuals.reserve(before.size());
    for (const XiDev &d : before)
        residuals.append(d.digitizer.r);

    const QString bin = QStandardPaths::findExecutable(QStringLiteral("xrandr"));
    if (bin.isEmpty()) {
        if (error)
            *error = QStringLiteral("xrandr not found");
        return false;
    }
    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments({QStringLiteral("--output"), live.name, QStringLiteral("--rotate"),
                       xrandrRotateArg(want)});
    proc.start();
    if (!proc.waitForFinished(15000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("xrandr timed out");
        return false;
    }
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("xrandr failed") : err;
        return false;
    }

    OutputInfo now = readOutput();
    if (!now.ok || normalizeKscreen(now.tKscreen) != want) {
        if (error)
            *error = QStringLiteral("RandR did not change T to %1").arg(want);
        return false;
    }

    QString writeErr;
    for (int i = 0; i < before.size(); ++i) {
        XiDev again = findDevice(before.at(i).digitizer);
        if (!again.digitizer.ok)
            again = before.at(i);
        writeResidual(again, residuals.at(i), &writeErr);
    }
    return true;
}

QVector<Digitizer> X11Backend::listDigitizers()
{
    QVector<Digitizer> out;
    const QVector<XiDev> all = scanDevices();
    for (const XiDev &d : all)
        out.append(d.digitizer);
    return out;
}

Digitizer X11Backend::resolve(DigitizerClass kind, const Digitizer *identity)
{
    const QVector<XiDev> all = scanDevices();
    if (identity && identity->ok && !identity->name.isEmpty()) {
        for (const XiDev &d : all) {
            if (d.digitizer.name == identity->name && d.digitizer.vendor == identity->vendor
                && d.digitizer.product == identity->product)
                return d.digitizer;
        }
    }
    Digitizer elan;
    Digitizer firstTouch;
    Digitizer stylusStylus;
    Digitizer firstStylus;
    for (const XiDev &d : all) {
        if (kind == DigitizerClass::Finger) {
            if (d.eraser || d.stylus)
                continue;
            if (!d.touch && !d.digitizer.name.contains(QLatin1String("Touchscreen"), Qt::CaseInsensitive)
                && !d.digitizer.name.contains(QLatin1String("Elan"), Qt::CaseInsensitive))
                continue;
            if (!firstTouch.ok)
                firstTouch = d.digitizer;
            if (!elan.ok && d.digitizer.name.contains(QLatin1String("Elan"), Qt::CaseInsensitive))
                elan = d.digitizer;
        } else {
            if (d.eraser || !d.stylus)
                continue;
            if (!firstStylus.ok)
                firstStylus = d.digitizer;
            if (!stylusStylus.ok
                && d.digitizer.name.contains(QLatin1String("Stylus stylus"), Qt::CaseInsensitive))
                stylusStylus = d.digitizer;
        }
    }
    if (kind == DigitizerClass::Finger)
        return elan.ok ? elan : firstTouch;
    return stylusStylus.ok ? stylusStylus : firstStylus;
}

bool X11Backend::writeResidual(const XiDev &dev, int r, QString *error)
{
    if (!dev.digitizer.ok || dev.deviceId < 0) {
        if (error)
            *error = QStringLiteral("no XInput device");
        return false;
    }
    const OutputInfo out = readOutput();
    const QString t = out.tKscreen.isEmpty() ? QStringLiteral("none") : out.tKscreen;
    if (dev.hasWacomRotation) {
        float identity[9];
        composeCtm(QStringLiteral("none"), 0, identity);
        writeCtm(dev.deviceId, identity);
        if (!writeWacomRotation(dev.deviceId, composeWacom(t, r))) {
            if (error)
                *error = QStringLiteral("Wacom Rotation write failed");
            return false;
        }
        return true;
    }
    float ctm[9];
    composeCtm(t, r, ctm);
    if (!writeCtm(dev.deviceId, ctm)) {
        if (error)
            *error = QStringLiteral("CTM write failed");
        return false;
    }
    return true;
}

bool X11Backend::writeErasers(int r, QString *error)
{
    bool all = true;
    const QVector<XiDev> allDevs = scanDevices();
    for (const XiDev &d : allDevs) {
        if (!d.eraser)
            continue;
        if (!writeResidual(d, r, error))
            all = false;
    }
    return all;
}

bool X11Backend::setResidual(const Digitizer &dev, int r, QString *error)
{
    XiDev found = findDevice(dev);
    if (!found.digitizer.ok) {
        if (error)
            *error = QStringLiteral("digitizer not found");
        return false;
    }
    if (!writeResidual(found, r, error))
        return false;
    if (found.stylus)
        writeErasers(r, error);
    return true;
}

int X11Backend::residualAt(int deviceId, bool hasWacom, bool *ok) const
{
    if (ok)
        *ok = false;
    if (deviceId < 0)
        return 0;
    const OutputInfo out = peekOutput();
    const QString t = out.tKscreen.isEmpty() ? QStringLiteral("none") : out.tKscreen;
    if (hasWacom) {
        int w = 0;
        if (!readWacomRotation(deviceId, &w))
            return 0;
        if (ok)
            *ok = true;
        return classifyWacomResidual(w, t);
    }
    float ctm[9];
    if (!readCtm(deviceId, ctm))
        return 0;
    if (ok)
        *ok = true;
    return classifyCtmResidual(ctm, t);
}

int X11Backend::getResidual(const Digitizer &dev, bool *ok)
{
    if (ok)
        *ok = false;
    XiDev found = findDevice(dev);
    if (!found.digitizer.ok && !dev.sysName.isEmpty()) {
        bool parsed = false;
        const int id = dev.sysName.toInt(&parsed);
        if (parsed) {
            found.deviceId = id;
            found.hasWacomRotation = hasProperty(id, "Wacom Rotation");
            found.digitizer.ok = true;
        }
    }
    if (found.deviceId < 0)
        return 0;
    return residualAt(found.deviceId, found.hasWacomRotation, ok);
}

bool X11Backend::stampFollow(const HomeProfile &home, QString *error)
{
    if (clinicHoldActive())
        return true;
    Digitizer wantF;
    wantF.name = home.fingerName;
    wantF.vendor = home.fingerVendor;
    wantF.product = home.fingerProduct;
    wantF.ok = !home.fingerName.isEmpty();
    Digitizer wantP;
    wantP.name = home.penName;
    wantP.vendor = home.penVendor;
    wantP.product = home.penProduct;
    wantP.ok = !home.penName.isEmpty();
    Digitizer finger = resolve(DigitizerClass::Finger, wantF.ok ? &wantF : nullptr);
    Digitizer pen = resolve(DigitizerClass::Pen, wantP.ok ? &wantP : nullptr);
    bool ok = true;
    if (finger.ok && !setResidual(finger, home.rTouch, error))
        ok = false;
    if (pen.ok && !setResidual(pen, home.rPen, error))
        ok = false;
    else if (!pen.ok)
        writeErasers(home.rPen, error);
    return ok;
}

void X11Backend::watchPose()
{
    if (!m_dpy || m_watching)
        return;
    m_watching = true;
    XRRSelectInput(asDisplay(m_dpy), DefaultRootWindow(asDisplay(m_dpy)),
                   RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask | RROutputChangeNotifyMask);
    XFlush(asDisplay(m_dpy));
    m_notifier = new QSocketNotifier(ConnectionNumber(asDisplay(m_dpy)), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this](QSocketDescriptor) {
        onX11Readable();
    });
    bindScreens();
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        connect(gui, &QGuiApplication::screenAdded, this, [this](QScreen *) { bindScreens(); });
}

void X11Backend::bindScreens()
{
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        connect(screen, &QScreen::geometryChanged, this, &OrientationBackend::poseChanged,
                Qt::UniqueConnection);
        connect(screen, &QScreen::orientationChanged, this, &OrientationBackend::poseChanged,
                Qt::UniqueConnection);
        connect(screen, &QScreen::primaryOrientationChanged, this,
                &OrientationBackend::poseChanged, Qt::UniqueConnection);
    }
}

void X11Backend::onX11Readable()
{
    if (!m_dpy)
        return;
    XEvent event;
    bool changed = false;
    while (XPending(asDisplay(m_dpy))) {
        XNextEvent(asDisplay(m_dpy), &event);
        if (event.type == m_rrEventBase + RRScreenChangeNotify) {
            XRRUpdateConfiguration(&event);
            changed = true;
        } else if (event.type == m_rrEventBase + RRNotify)
            changed = true;
    }
    if (changed)
        emit poseChanged();
}

} // namespace TiltBack
