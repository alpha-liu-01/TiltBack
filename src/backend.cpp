#include "backend.h"
#include "gnomebackend.h"
#include "kwinbackend.h"

#ifdef TILTBACK_X11
#include "x11backend.h"
#endif

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <cmath>

namespace TiltBack {
namespace {

void ctmMul(const float *a, const float *b, float *out)
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i * 3 + j] = a[i * 3 + 0] * b[0 * 3 + j] + a[i * 3 + 1] * b[1 * 3 + j]
                + a[i * 3 + 2] * b[2 * 3 + j];
        }
    }
}

float ctmDist(const float *a, const float *b)
{
    float s = 0;
    for (int i = 0; i < 9; ++i) {
        const float d = a[i] - b[i];
        s += d * d;
    }
    return s;
}

} // namespace

bool isBuiltinOutput(const QString &name)
{
    return name.startsWith(QLatin1String("eDP"))
        || name.startsWith(QLatin1String("DSI"))
        || name.startsWith(QLatin1String("LVDS"));
}

QString normalizeKscreen(const QString &requested)
{
    const QString t = requested.trimmed().toLower();
    if (t == QLatin1String("left") || t == QLatin1String("rotated90"))
        return QStringLiteral("left");
    if (t == QLatin1String("right") || t == QLatin1String("rotated270"))
        return QStringLiteral("right");
    if (t == QLatin1String("none") || t == QLatin1String("normal"))
        return QStringLiteral("none");
    if (t == QLatin1String("inverted") || t == QLatin1String("rotated180"))
        return QStringLiteral("inverted");
    return {};
}

QString kwinNameFromKscreen(const QString &kscreen)
{
    const QString t = normalizeKscreen(kscreen);
    if (t == QLatin1String("left"))
        return QStringLiteral("Rotated90");
    if (t == QLatin1String("right"))
        return QStringLiteral("Rotated270");
    if (t == QLatin1String("inverted"))
        return QStringLiteral("Rotated180");
    if (t == QLatin1String("none"))
        return QStringLiteral("Normal");
    return t;
}

bool sessionLooksX11()
{
    const QString wayland = qEnvironmentVariable("WAYLAND_DISPLAY");
    const QString display = qEnvironmentVariable("DISPLAY");
    return wayland.isEmpty() && !display.isEmpty();
}

bool kwinBusAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusInterface iface(QStringLiteral("org.kde.KWin"),
                         QStringLiteral("/org/kde/KWin/InputDevice"),
                         QStringLiteral("org.freedesktop.DBus.Properties"), bus);
    return iface.isValid();
}

bool mutterBusAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusInterface iface(QStringLiteral("org.gnome.Mutter.DisplayConfig"),
                         QStringLiteral("/org/gnome/Mutter/DisplayConfig"),
                         QStringLiteral("org.gnome.Mutter.DisplayConfig"), bus);
    return iface.isValid();
}

void ctmForKscreen(const QString &kscreen, float *m)
{
    const QString t = normalizeKscreen(kscreen);
    // Row-major 3x3. Matches xinput / libinput "map to rotated output".
    const float none[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float left[] = {0, -1, 1, 1, 0, 0, 0, 0, 1};
    const float right[] = {0, 1, 0, -1, 0, 1, 0, 0, 1};
    const float inverted[] = {-1, 0, 1, 0, -1, 1, 0, 0, 1};
    const float *src = none;
    if (t == QLatin1String("left"))
        src = left;
    else if (t == QLatin1String("right"))
        src = right;
    else if (t == QLatin1String("inverted"))
        src = inverted;
    for (int i = 0; i < 9; ++i)
        m[i] = src[i];
}

void ctmForResidual(int r, float *m)
{
    switch (r) {
    case 1:
        ctmForKscreen(QStringLiteral("left"), m);
        break;
    case 2:
        ctmForKscreen(QStringLiteral("right"), m);
        break;
    case 4:
    case 8:
        ctmForKscreen(QStringLiteral("inverted"), m);
        break;
    default:
        ctmForKscreen(QStringLiteral("none"), m);
        break;
    }
}

void composeCtm(const QString &tKscreen, int r, float *out)
{
    float t[9];
    float extra[9];
    ctmForKscreen(tKscreen, t);
    ctmForResidual(r, extra);
    ctmMul(t, extra, out);
}

int classifyCtmResidual(const float *ctm, const QString &tKscreen)
{
    float t[9];
    float tInv[9];
    ctmForKscreen(tKscreen.isEmpty() ? QStringLiteral("none") : tKscreen, t);
    // These CTMs are involutions up to the 90° pair: invert = transpose of linear part
    // with the same translation recipe. Compute R = T^{-1} * CTM.
    ctmForKscreen(tKscreen.isEmpty() ? QStringLiteral("none") : tKscreen, tInv);
    if (normalizeKscreen(tKscreen) == QLatin1String("left"))
        ctmForKscreen(QStringLiteral("right"), tInv);
    else if (normalizeKscreen(tKscreen) == QLatin1String("right"))
        ctmForKscreen(QStringLiteral("left"), tInv);
    // none and inverted are self-inverse
    float residual[9];
    ctmMul(tInv, ctm, residual);
    // 4 and 8 share the invert CTM; prefer 8 (InvertedLandscape), the clinic invert.
    const int candidates[] = {0, 1, 2, 8, 4};
    int best = 0;
    float bestD = 1e9f;
    for (int c : candidates) {
        float want[9];
        ctmForResidual(c, want);
        const float d = ctmDist(residual, want);
        if (d < bestD) {
            bestD = d;
            best = c;
        }
    }
    return best;
}

class NullBackend final : public OrientationBackend
{
public:
    using OrientationBackend::OrientationBackend;
    QString id() const override { return QStringLiteral("none"); }
    QString pictureBackendLabel() const override { return QStringLiteral("no"); }
    QString inputBackendLabel() const override { return QStringLiteral("no"); }
    QString pictureSource(const OutputInfo &) const override
    {
        return QStringLiteral("no session backend (not KWin, not X11, not Mutter)");
    }
    QString inputSource(const Digitizer &) const override
    {
        return QStringLiteral("no session backend");
    }
    bool canChangePicture() const override { return false; }
    bool canChangeInput() const override { return false; }
    bool persistKcminput() const override { return false; }
    QString persistHow() const override { return QStringLiteral("no persist backend"); }
    OutputInfo readOutput() override { return {}; }
    bool setOutput(const QString &, QString *error) override
    {
        if (error)
            *error = QStringLiteral("no picture backend");
        return false;
    }
    QVector<Digitizer> listDigitizers() override { return {}; }
    Digitizer resolve(DigitizerClass, const Digitizer *) override { return {}; }
    bool setResidual(const Digitizer &, int, QString *error) override
    {
        if (error)
            *error = QStringLiteral("no input backend");
        return false;
    }
    int getResidual(const Digitizer &, bool *ok) override
    {
        if (ok)
            *ok = false;
        return 0;
    }
    bool stampFollow(const HomeProfile &, QString *error) override
    {
        if (error)
            *error = QStringLiteral("no follow backend");
        return false;
    }
    void watchPose() override {}
};

OrientationBackend *createBackend(QObject *parent)
{
#ifdef TILTBACK_X11
    if (sessionLooksX11()) {
        auto *x11 = new X11Backend(parent);
        if (x11->isOpen())
            return x11;
        delete x11;
    }
#endif
    if (kwinBusAvailable())
        return new KwinBackend(parent);
    if (mutterBusAvailable())
        return new GnomeBackend(parent);
    return new NullBackend(parent);
}

} // namespace TiltBack
