#include "clinicmodel.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace {

QString readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString orientationName(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("Portrait");
    case 2:
        return QStringLiteral("Landscape");
    case 4:
        return QStringLiteral("InvertedPortrait");
    case 8:
        return QStringLiteral("InvertedLandscape");
    case 0:
        return QStringLiteral("Primary");
    default:
        return QStringLiteral("raw:%1").arg(value);
    }
}

QString panelOrientationName(int value)
{
    switch (value) {
    case -1:
        return QStringLiteral("UNKNOWN");
    case 0:
        return QStringLiteral("NORMAL");
    case 1:
        return QStringLiteral("BOTTOM_UP");
    case 2:
        return QStringLiteral("LEFT_UP");
    case 3:
        return QStringLiteral("RIGHT_UP");
    default:
        return QStringLiteral("raw:%1").arg(value);
    }
}

using TiltBack::isDenied;
using TiltBack::unwrap;
using TiltBack::vidPid;

bool isBuiltinOutput(const QString &name)
{
    return name.startsWith(QLatin1String("eDP"))
        || name.startsWith(QLatin1String("DSI"))
        || name.startsWith(QLatin1String("LVDS"));
}

QString findEdpTransform(const QJsonValue &value, QString *fromName)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();
        const QString transform = obj.value(QStringLiteral("transform")).toString();
        if (!transform.isEmpty() && (isBuiltinOutput(name) || name.isEmpty())) {
            if (fromName && !name.isEmpty())
                *fromName = name;
            if (isBuiltinOutput(name))
                return transform;
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString found = findEdpTransform(it.value(), fromName);
            if (!found.isEmpty())
                return found;
        }
        if (!transform.isEmpty()) {
            if (fromName && !name.isEmpty())
                *fromName = name;
            return transform;
        }
    } else if (value.isArray()) {
        QString fallback;
        QString fallbackName;
        for (const QJsonValue &item : value.toArray()) {
            QString name;
            const QString found = findEdpTransform(item, &name);
            if (found.isEmpty())
                continue;
            if (isBuiltinOutput(name)) {
                if (fromName)
                    *fromName = name;
                return found;
            }
            if (fallback.isEmpty()) {
                fallback = found;
                fallbackName = name;
            }
        }
        if (!fallback.isEmpty()) {
            if (fromName)
                *fromName = fallbackName;
            return fallback;
        }
    }
    return {};
}

QVariantMap toMap(const QVariant &value)
{
    const QVariant v = unwrap(value);
    if (v.canConvert<QVariantMap>())
        return v.toMap();
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    return {};
}

QVariantList toList(const QVariant &value)
{
    const QVariant v = unwrap(value);
    if (v.canConvert<QVariantList>())
        return v.toList();
    if (!v.canConvert<QDBusArgument>())
        return {};
    const QDBusArgument arg = v.value<QDBusArgument>();
    if (arg.currentType() == QDBusArgument::ArrayType)
        return qdbus_cast<QVariantList>(arg);
    return {};
}

QString kwinNameFromRotation(int rotation)
{
    switch (rotation) {
    case 1:
        return QStringLiteral("Normal");
    case 2:
        return QStringLiteral("Rotated90");
    case 4:
        return QStringLiteral("Rotated180");
    case 8:
        return QStringLiteral("Rotated270");
    default:
        return QStringLiteral("raw:%1").arg(rotation);
    }
}

QString kscreenNameFromRotation(int rotation)
{
    switch (rotation) {
    case 1:
        return QStringLiteral("none");
    case 2:
        return QStringLiteral("left");
    case 4:
        return QStringLiteral("inverted");
    case 8:
        return QStringLiteral("right");
    default:
        return {};
    }
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

} // namespace

ClinicModel::ClinicModel(QObject *parent)
    : QObject(parent)
    , m_revertTimer(new QTimer(this))
{
    m_revertTimer->setInterval(1000);
    connect(m_revertTimer, &QTimer::timeout, this, &ClinicModel::onRevertTick);
    refresh();
}

void ClinicModel::refresh()
{
    m_pictureError.clear();
    m_fingerError.clear();
    m_penError.clear();
    probeDmi();
    probeDrm();
    probeOutputTransform();
    probeKwinInputs();
    loadHomeState();
    refreshFollowStatus();
    buildReport();
    emit changed();
}

void ClinicModel::copyReport()
{
    if (QClipboard *clip = QGuiApplication::clipboard())
        clip->setText(m_reportText);
}

void ClinicModel::probeDmi()
{
    m_dmiVendor = readSysfs(QStringLiteral("/sys/class/dmi/id/sys_vendor"));
    m_dmiProduct = readSysfs(QStringLiteral("/sys/class/dmi/id/product_name"));
    m_dmiBoard = readSysfs(QStringLiteral("/sys/class/dmi/id/board_name"));
}

void ClinicModel::probeDrm()
{
    m_panelOrientation.clear();
    const QDir dri(QStringLiteral("/dev/dri"));
    const QStringList cards = dri.entryList(QStringList() << QStringLiteral("card*"), QDir::System);
    for (const QString &card : cards) {
        if (card.startsWith(QLatin1String("card")) && card.contains(QLatin1Char('-')))
            continue;
        const QByteArray path = dri.absoluteFilePath(card).toUtf8();
        const int fd = open(path.constData(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        drmModeRes *res = drmModeGetResources(fd);
        if (!res) {
            close(fd);
            continue;
        }
        for (int i = 0; i < res->count_connectors; ++i) {
            drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
            if (!conn)
                continue;
            const bool edp = conn->connector_type == DRM_MODE_CONNECTOR_eDP
                || conn->connector_type == DRM_MODE_CONNECTOR_LVDS
                || conn->connector_type == DRM_MODE_CONNECTOR_DSI;
            if (!edp) {
                drmModeFreeConnector(conn);
                continue;
            }
            drmModeObjectProperties *props = drmModeObjectGetProperties(
                fd, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR);
            if (props) {
                for (uint32_t p = 0; p < props->count_props; ++p) {
                    drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[p]);
                    if (!prop)
                        continue;
                    if (QByteArray(prop->name) == "panel orientation") {
                        const int raw = static_cast<int>(props->prop_values[p]);
                        m_panelOrientation = QStringLiteral("%1 (%2)")
                                                 .arg(panelOrientationName(raw))
                                                 .arg(raw);
                    }
                    drmModeFreeProperty(prop);
                }
                drmModeFreeObjectProperties(props);
            }
            drmModeFreeConnector(conn);
            if (!m_panelOrientation.isEmpty())
                break;
        }
        drmModeFreeResources(res);
        close(fd);
        if (!m_panelOrientation.isEmpty())
            break;
    }
    if (m_panelOrientation.isEmpty())
        m_panelOrientation = QStringLiteral("unavailable");
}

void ClinicModel::probeOutputTransform()
{
    m_persistedTransform.clear();
    const QString path = QDir::home().filePath(QStringLiteral(".config/kwinoutputconfig.json"));
    QFile f(path);
    QString fromName;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        m_persistedTransform = findEdpTransform(root, &fromName);
    }

    if (!readLiveOutput()) {
        if (m_outputTransform.isEmpty())
            m_outputTransform = m_persistedTransform.isEmpty()
                ? QStringLiteral("unknown")
                : m_persistedTransform;
        if (m_pictureError.isEmpty())
            m_pictureError = QStringLiteral("KScreen getConfig unavailable");
    }

    fillPictureCard();
    if (!fromName.isEmpty() && m_outputName.isEmpty())
        m_outputName = fromName;
}

void ClinicModel::fillPictureCard()
{
    m_pictureValue = QStringLiteral("T=%1").arg(m_outputTransform);
    QString detail = QStringLiteral("%1 %2\nboard %3\npanel-orientation %4")
                         .arg(m_dmiVendor, m_dmiProduct, m_dmiBoard, m_panelOrientation);
    if (!m_persistedTransform.isEmpty() && m_persistedTransform != m_outputTransform)
        detail += QStringLiteral("\npersisted %1 (file, may be stale)").arg(m_persistedTransform);
    if (!m_pictureError.isEmpty())
        detail += QStringLiteral("\n%1").arg(m_pictureError);
    m_pictureDetail = detail;
    const QString live = m_outputName.isEmpty()
        ? QStringLiteral("KScreen getConfig (live)")
        : QStringLiteral("KScreen getConfig (live, %1)").arg(m_outputName);
    if (m_persistedTransform.isEmpty() || m_persistedTransform == m_outputTransform)
        m_pictureSource = live + QStringLiteral(" + DMI + DRM");
    else
        m_pictureSource = live
            + QStringLiteral(" + persisted kwinoutputconfig.json=%1").arg(m_persistedTransform);
}

void ClinicModel::probeKwinInputs()
{
    m_fingerValue = QStringLiteral("no touchscreen");
    m_fingerDetail.clear();
    m_fingerSource = QStringLiteral("org.kde.KWin InputDevice");
    m_penValue = QStringLiteral("no stylus");
    m_penDetail.clear();
    m_penSource = QStringLiteral("org.kde.KWin InputDevice");
    m_reportDevices.clear();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_fingerValue = QStringLiteral("no session bus");
        m_penValue = QStringLiteral("no session bus");
        return;
    }

    QDBusInterface mgr(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/InputDevice"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        bus);
    const QDBusReply<QDBusVariant> namesReply = mgr.call(
        QStringLiteral("Get"),
        QStringLiteral("org.kde.KWin.InputDeviceManager"),
        QStringLiteral("devicesSysNames"));
    if (!namesReply.isValid()) {
        m_fingerValue = QStringLiteral("KWin InputDevice unavailable");
        m_penValue = m_fingerValue;
        m_fingerDetail = namesReply.error().message();
        m_penDetail = m_fingerDetail;
        return;
    }

    QStringList sysNames;
    const QVariant namesVar = namesReply.value().variant();
    if (namesVar.canConvert<QStringList>())
        sysNames = namesVar.toStringList();
    else if (namesVar.canConvert<QDBusArgument>())
        sysNames = qdbus_cast<QStringList>(namesVar.value<QDBusArgument>());
    if (sysNames.isEmpty()) {
        m_fingerValue = QStringLiteral("no InputDevice names");
        m_penValue = m_fingerValue;
        return;
    }
    m_finger = {};
    m_pen = {};
    QStringList reportLines;

    for (const QString &sys : sysNames) {
        const QString path = QStringLiteral("/org/kde/KWin/InputDevice/%1").arg(sys);
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            path,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("GetAll"));
        msg << QStringLiteral("org.kde.KWin.InputDevice");
        const QDBusMessage reply = bus.call(msg);
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
            continue;
        QVariantMap all;
        const QVariant first = reply.arguments().at(0);
        if (first.canConvert<QVariantMap>())
            all = first.toMap();
        else if (first.canConvert<QDBusArgument>())
            all = qdbus_cast<QVariantMap>(first.value<QDBusArgument>());
        if (all.isEmpty())
            continue;
        const QString name = unwrap(all.value(QStringLiteral("name"))).toString();
        const bool touch = unwrap(all.value(QStringLiteral("touch"))).toBool();
        const bool tabletTool = unwrap(all.value(QStringLiteral("tabletTool"))).toBool();
        const bool touchpad = unwrap(all.value(QStringLiteral("touchpad"))).toBool();
        const int r = unwrap(all.value(QStringLiteral("orientationDBus"))).toInt();
        const quint32 vendor = unwrap(all.value(QStringLiteral("vendor"))).toUInt();
        const quint32 product = unwrap(all.value(QStringLiteral("product"))).toUInt();
        const QString id = vidPid(vendor, product);
        const bool denied = isDenied(name, touchpad, vendor, product);

        if ((touch || tabletTool) && !denied) {
            reportLines.append(QStringLiteral("%1  %2  R=%3 (%4)  sys=%5")
                                   .arg(name, id)
                                   .arg(r)
                                   .arg(orientationName(r), sys));
        }

        if (!m_finger.ok && touch && !denied) {
            m_finger.name = name;
            m_finger.vendor = vendor;
            m_finger.product = product;
            m_finger.sysName = sys;
            m_finger.path = path;
            m_finger.r = r;
            m_finger.ok = true;
        }
        if (!m_pen.ok && tabletTool && !denied) {
            m_pen.name = name;
            m_pen.vendor = vendor;
            m_pen.product = product;
            m_pen.sysName = sys;
            m_pen.path = path;
            m_pen.r = r;
            m_pen.ok = true;
        }
    }

    m_reportDevices = reportLines.join(QLatin1Char('\n'));
    fillFingerCard();
    fillPenCard();
}

void ClinicModel::fillFingerCard()
{
    if (!m_finger.ok) {
        if (m_fingerValue.isEmpty())
            m_fingerValue = QStringLiteral("no touchscreen");
        return;
    }
    m_fingerValue = QStringLiteral("R=%1 (%2)").arg(m_finger.r).arg(orientationName(m_finger.r));
    m_fingerDetail = QStringLiteral("%1\n%2").arg(m_finger.name, vidPid(m_finger.vendor, m_finger.product));
    if (!m_fingerError.isEmpty())
        m_fingerDetail += QLatin1Char('\n') + m_fingerError;
    m_fingerSource = QStringLiteral("KWin InputDevice GetAll (name + VID:PID; %1 is not identity)")
                         .arg(m_finger.sysName);
}

void ClinicModel::fillPenCard()
{
    if (!m_pen.ok) {
        if (m_penValue.isEmpty())
            m_penValue = QStringLiteral("no stylus");
        return;
    }
    m_penValue = QStringLiteral("R=%1 (%2)").arg(m_pen.r).arg(orientationName(m_pen.r));
    m_penDetail = QStringLiteral("%1\n%2").arg(m_pen.name, vidPid(m_pen.vendor, m_pen.product));
    if (!m_penError.isEmpty())
        m_penDetail += QLatin1Char('\n') + m_penError;
    m_penSource = QStringLiteral("KWin InputDevice GetAll (name + VID:PID; %1 is not identity)")
                      .arg(m_pen.sysName);
}

void ClinicModel::buildReport()
{
    QStringList lines;
    lines << QStringLiteral("TiltBack clinic report")
          << QStringLiteral("DMI: %1 / %2 / %3").arg(m_dmiVendor, m_dmiProduct, m_dmiBoard)
          << QStringLiteral("DRM panel-orientation: %1").arg(m_panelOrientation)
          << QStringLiteral("T live: %1").arg(m_outputTransform);
    if (!m_persistedTransform.isEmpty())
        lines << QStringLiteral("T persisted: %1").arg(m_persistedTransform);
    lines << QStringLiteral("Arrow: not inverted / not probed")
          << m_homeLine
          << m_persistLine
          << m_followStatus
          << QStringLiteral("Absolute devices:");
    if (m_reportDevices.isEmpty())
        lines << QStringLiteral("(none)");
    else
        lines << m_reportDevices;
    m_reportText = lines.join(QLatin1Char('\n'));
}

bool ClinicModel::ensureKscreenBackend()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusMessage ping = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KScreen"),
        QStringLiteral("/backend"),
        QStringLiteral("org.kde.kscreen.Backend"),
        QStringLiteral("getConfig"));
    const QDBusMessage pingReply = bus.call(ping);
    if (pingReply.type() != QDBusMessage::ErrorMessage)
        return true;

    QDBusMessage req = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KScreen"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.KScreen"),
        QStringLiteral("requestBackend"));
    req << QString() << QVariantMap();
    const QDBusMessage reqReply = bus.call(req);
    if (reqReply.type() == QDBusMessage::ErrorMessage)
        return false;
    if (!reqReply.arguments().isEmpty() && !reqReply.arguments().at(0).toBool())
        return false;
    return true;
}

bool ClinicModel::readLiveOutput()
{
    if (!ensureKscreenBackend())
        return false;

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KScreen"),
        QStringLiteral("/backend"),
        QStringLiteral("org.kde.kscreen.Backend"),
        QStringLiteral("getConfig"));
    const QDBusMessage reply = bus.call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return false;

    const QVariantMap config = toMap(reply.arguments().at(0));
    const QVariantList outputs = toList(config.value(QStringLiteral("outputs")));
    QVariantMap chosen;
    QVariantMap fallback;
    for (const QVariant &item : outputs) {
        const QVariantMap out = toMap(item);
        if (out.isEmpty())
            continue;
        const QString name = unwrap(out.value(QStringLiteral("name"))).toString();
        const bool enabled = unwrap(out.value(QStringLiteral("enabled"))).toBool();
        if (!enabled)
            continue;
        if (fallback.isEmpty())
            fallback = out;
        if (isBuiltinOutput(name)) {
            chosen = out;
            break;
        }
    }
    if (chosen.isEmpty())
        chosen = fallback;
    if (chosen.isEmpty())
        return false;

    const QString name = unwrap(chosen.value(QStringLiteral("name"))).toString();
    const int rotation = unwrap(chosen.value(QStringLiteral("rotation"))).toInt();
    m_outputName = name;
    m_liveKscreen = kscreenNameFromRotation(rotation);
    m_outputTransform = kwinNameFromRotation(rotation);
    return !m_liveKscreen.isEmpty();
}

bool ClinicModel::runDoctor(const QString &output, const QString &kscreen)
{
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (bin.isEmpty()) {
        m_pictureError = QStringLiteral("kscreen-doctor not found");
        return false;
    }
    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments({QStringLiteral("output.%1.rotation.%2").arg(output, kscreen)});
    proc.start();
    if (!proc.waitForFinished(15000)) {
        proc.kill();
        m_pictureError = QStringLiteral("kscreen-doctor timed out");
        return false;
    }
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString combined = err.isEmpty() ? out : err;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0
        || combined.contains(QLatin1String("not found"), Qt::CaseInsensitive)) {
        m_pictureError = combined.isEmpty() ? QStringLiteral("kscreen-doctor failed") : combined;
        return false;
    }
    m_pictureError.clear();
    return true;
}

void ClinicModel::applyPicture(const QString &kscreen)
{
    const QString want = normalizeKscreen(kscreen);
    if (want.isEmpty()) {
        m_pictureError = QStringLiteral("unknown transform %1").arg(kscreen);
        emit changed();
        return;
    }

    const bool hadLive = readLiveOutput();
    if (m_outputName.isEmpty())
        m_outputName = QStringLiteral("eDP-1");
    if (hadLive && m_liveKscreen == want) {
        m_pictureError = QStringLiteral("already %1").arg(want);
        fillPictureCard();
        emit changed();
        return;
    }

    const QString snapshot = !m_liveKscreen.isEmpty()
        ? m_liveKscreen
        : normalizeKscreen(m_outputTransform);

    if (!runDoctor(m_outputName, want)) {
        const QString err = m_pictureError;
        refresh();
        m_pictureError = err;
        fillPictureCard();
        emit changed();
        return;
    }
    if (!readLiveOutput() || m_liveKscreen != want) {
        const QString err = m_pictureError.isEmpty()
            ? QStringLiteral("KScreen did not change T to %1").arg(want)
            : m_pictureError;
        refresh();
        m_pictureError = err;
        fillPictureCard();
        emit changed();
        return;
    }

    m_revertKscreen = snapshot;
    m_picturePendingLabel = QStringLiteral("T=%1").arg(m_outputTransform);
    startPictureCountdown();
    refresh();
}

void ClinicModel::keepPicture()
{
    if (!m_picturePending)
        return;
    stopPictureCountdown();
    emit changed();
}

void ClinicModel::revertPicture()
{
    if (!m_picturePending)
        return;
    const QString target = m_revertKscreen;
    stopPictureCountdown();
    if (target.isEmpty()) {
        refresh();
        return;
    }
    if (m_outputName.isEmpty())
        m_outputName = QStringLiteral("eDP-1");
    runDoctor(m_outputName, target);
    refresh();
}

bool ClinicModel::anyPending() const
{
    return m_picturePending || m_fingerPending || m_penPending;
}

void ClinicModel::ensureTimer()
{
    if (!m_revertTimer->isActive())
        m_revertTimer->start();
}

void ClinicModel::startPictureCountdown()
{
    m_picturePending = true;
    m_pictureSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopPictureCountdown()
{
    m_picturePending = false;
    m_pictureSeconds = 0;
    m_picturePendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

void ClinicModel::startFingerCountdown()
{
    m_fingerPending = true;
    m_fingerSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopFingerCountdown()
{
    m_fingerPending = false;
    m_fingerSeconds = 0;
    m_fingerPendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

void ClinicModel::startPenCountdown()
{
    m_penPending = true;
    m_penSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopPenCountdown()
{
    m_penPending = false;
    m_penSeconds = 0;
    m_penPendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

void ClinicModel::onRevertTick()
{
    if (m_picturePending) {
        m_pictureSeconds--;
        if (m_pictureSeconds <= 0)
            revertPicture();
    }
    if (m_fingerPending) {
        m_fingerSeconds--;
        if (m_fingerSeconds <= 0)
            revertFinger();
        else
            warnIfFollowFight(DigitizerClass::Finger);
    }
    if (m_penPending) {
        m_penSeconds--;
        if (m_penSeconds <= 0)
            revertPen();
        else
            warnIfFollowFight(DigitizerClass::Pen);
    }
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
    emit changed();
}

bool ClinicModel::resolveDigitizer(DigitizerClass kind, Digitizer *out)
{
    if (!out)
        return false;
    const Digitizer *identity = nullptr;
    if (kind == DigitizerClass::Finger && m_finger.ok)
        identity = &m_finger;
    else if (kind == DigitizerClass::Pen && m_pen.ok)
        identity = &m_pen;
    *out = TiltBack::resolveDigitizer(kind, identity);
    return out->ok;
}

bool ClinicModel::setOrientation(const QString &path, int r, QString *error)
{
    return TiltBack::setOrientation(path, r, error);
}

int ClinicModel::getOrientation(const QString &path, bool *ok)
{
    return TiltBack::getOrientation(path, ok);
}

void ClinicModel::applyDigitizer(DigitizerClass kind, int r)
{
    const bool finger = kind == DigitizerClass::Finger;
    QString &err = finger ? m_fingerError : m_penError;
    Digitizer &slot = finger ? m_finger : m_pen;

    Digitizer live;
    if (!resolveDigitizer(kind, &live)) {
        err = QStringLiteral("no named digitizer");
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }
    slot = live;

    if (live.r == r) {
        err = QStringLiteral("already R=%1").arg(r);
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    const int snapshot = live.r;
    if (!setOrientation(live.path, r, &err)) {
        const QString keep = err;
        refresh();
        err = keep;
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    bool ok = false;
    const int now = getOrientation(live.path, &ok);
    if (!ok || now != r) {
        err = QStringLiteral("KWin did not change R to %1").arg(r);
        refresh();
        if (finger)
            m_fingerError = QStringLiteral("KWin did not change R to %1").arg(r);
        else
            m_penError = QStringLiteral("KWin did not change R to %1").arg(r);
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    live.r = now;
    slot = live;
    if (finger) {
        m_fingerRevertR = snapshot;
        m_fingerAppliedR = r;
        m_fingerPendingLabel = QStringLiteral("R=%1 (%2)").arg(r).arg(orientationName(r));
        startFingerCountdown();
    } else {
        m_penRevertR = snapshot;
        m_penAppliedR = r;
        m_penPendingLabel = QStringLiteral("R=%1 (%2)").arg(r).arg(orientationName(r));
        startPenCountdown();
    }
    refresh();
    warnIfFollowFight(kind);
    if (kind == DigitizerClass::Finger)
        fillFingerCard();
    else
        fillPenCard();
    emit changed();
}

void ClinicModel::warnIfFollowFight(DigitizerClass kind)
{
    Digitizer live;
    if (!resolveDigitizer(kind, &live))
        return;
    const int applied = (kind == DigitizerClass::Finger) ? m_fingerAppliedR : m_penAppliedR;
    const bool pending = (kind == DigitizerClass::Finger) ? m_fingerPending : m_penPending;
    if (!pending || applied != 0 || live.r != 8)
        return;
    const QString warn = QStringLiteral("R jumped to 8 (is follow running?)");
    if (kind == DigitizerClass::Finger) {
        m_finger = live;
        m_fingerError = warn;
        fillFingerCard();
    } else {
        m_pen = live;
        m_penError = warn;
        fillPenCard();
    }
}

void ClinicModel::applyFinger(int r)
{
    applyDigitizer(DigitizerClass::Finger, r);
}

void ClinicModel::applyPen(int r)
{
    applyDigitizer(DigitizerClass::Pen, r);
}

void ClinicModel::keepFinger()
{
    if (!m_fingerPending)
        return;
    stopFingerCountdown();
    m_fingerError = QStringLiteral("Kept — follow restamps home R");
    fillFingerCard();
    emit changed();
}

void ClinicModel::keepPen()
{
    if (!m_penPending)
        return;
    stopPenCountdown();
    m_penError = QStringLiteral("Kept — follow restamps home R");
    fillPenCard();
    emit changed();
}

void ClinicModel::revertFinger()
{
    if (!m_fingerPending)
        return;
    const int target = m_fingerRevertR;
    stopFingerCountdown();
    Digitizer live;
    if (resolveDigitizer(DigitizerClass::Finger, &live))
        setOrientation(live.path, target, &m_fingerError);
    refresh();
}

void ClinicModel::revertPen()
{
    if (!m_penPending)
        return;
    const int target = m_penRevertR;
    stopPenCountdown();
    Digitizer live;
    if (resolveDigitizer(DigitizerClass::Pen, &live))
        setOrientation(live.path, target, &m_penError);
    refresh();
}

void ClinicModel::saveHome()
{
    readLiveOutput();
    probeKwinInputs();
    TiltBack::HomeProfile home;
    home.tHome = m_outputTransform;
    home.rTouch = m_finger.ok ? m_finger.r : 8;
    home.rPen = m_pen.ok ? m_pen.r : 8;
    home.fingerName = m_finger.name;
    home.fingerVendor = m_finger.vendor;
    home.fingerProduct = m_finger.product;
    home.penName = m_pen.name;
    home.penVendor = m_pen.vendor;
    home.penProduct = m_pen.product;
    m_home = home;

    QString homeErr;
    const bool homeOk = TiltBack::saveHomeFile(home, &homeErr);
    QString persistErr;
    const bool persistOk = TiltBack::persistKcminputrc(home, &persistErr);
    m_homeLine = QStringLiteral("Home  T=%1  R_touch=%2  R_pen=%3")
                     .arg(home.tHome)
                     .arg(home.rTouch)
                     .arg(home.rPen);
    if (persistOk)
        m_persistLine = QStringLiteral("persist ok (kcminputrc Orientation=)");
    else
        m_persistLine = QStringLiteral("persist failed: %1").arg(persistErr);
    if (!homeOk)
        m_persistLine += QStringLiteral(" — home.json: %1").arg(homeErr);
    else if (TiltBack::homeProfilePath().startsWith(QLatin1String("/tmp")))
        m_persistLine += QStringLiteral(" — home.json in /tmp");
    emit changed();
}

void ClinicModel::installFollow()
{
    QString err;
    if (TiltBack::installFollow(QCoreApplication::applicationFilePath(), &err) != 0)
        m_followStatus = QStringLiteral("Follow  install failed: %1").arg(err);
    else
        refreshFollowStatus();
    emit changed();
}

void ClinicModel::syncClinicHold()
{
    if (anyPending())
        TiltBack::writeClinicHold();
    else
        TiltBack::clearClinicHold();
}

void ClinicModel::loadHomeState()
{
    const bool haveFile = QFile::exists(TiltBack::homeProfilePath());
    m_home = TiltBack::loadHome(m_dmiProduct);
    if (m_home.tHome.isEmpty()) {
        m_homeLine = QStringLiteral("Home  (none — Save home or W620 DMI seed)");
        if (m_persistLine.isEmpty())
            m_persistLine = QStringLiteral("persist not written this session");
        return;
    }
    m_homeLine = QStringLiteral("Home  T=%1  R_touch=%2  R_pen=%3")
                     .arg(m_home.tHome)
                     .arg(m_home.rTouch)
                     .arg(m_home.rPen);
    if (m_persistLine.isEmpty()) {
        m_persistLine = haveFile
            ? QStringLiteral("home.json loaded")
            : QStringLiteral("W620 DMI seed — Save home to persist");
    }
}

void ClinicModel::refreshFollowStatus()
{
    auto unitState = [](const QString &unit) {
        QProcess p;
        p.start(QStringLiteral("systemctl"),
                {QStringLiteral("--user"), QStringLiteral("is-active"), unit});
        if (!p.waitForFinished(3000))
            return QStringLiteral("unknown");
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    };
    const QString follow = unitState(QStringLiteral("tiltback-follow.service"));
    const QString w620 = unitState(QStringLiteral("tiltback-w620.service"));
    if (w620 == QLatin1String("active"))
        m_followStatus = QStringLiteral("Follow  tiltback-w620.service active (Python) — Install to replace");
    else if (follow == QLatin1String("active"))
        m_followStatus = QStringLiteral("Follow  active");
    else
        m_followStatus = QStringLiteral("Follow  inactive — Install/start");
}
