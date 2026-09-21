#include "clinicmodel.h"

#include <QClipboard>
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

bool isDenied(const QString &name, bool touchpad)
{
    if (touchpad)
        return true;
    if (name.contains(QLatin1String("04e8:a00a")))
        return true;
    if (name.contains(QLatin1String("WCOM0028:00 2D1F:000C Mouse")))
        return true;
    return false;
}

QVariant unwrap(const QVariant &value)
{
    if (value.canConvert<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

QString vidPid(quint32 vendor, quint32 product)
{
    return QStringLiteral("%1:%2")
        .arg(vendor, 4, 16, QLatin1Char('0'))
        .arg(product, 4, 16, QLatin1Char('0'));
}

QString findEdpTransform(const QJsonValue &value, QString *fromName)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();
        const QString transform = obj.value(QStringLiteral("transform")).toString();
        if (!transform.isEmpty() && (name.startsWith(QLatin1String("eDP")) || name.isEmpty())) {
            if (fromName && !name.isEmpty())
                *fromName = name;
            if (name.startsWith(QLatin1String("eDP")))
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
            if (name.startsWith(QLatin1String("eDP"))) {
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
    probeDmi();
    probeDrm();
    probeOutputTransform();
    probeKwinInputs();
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
    bool haveFinger = false;
    bool havePen = false;
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
        const bool denied = isDenied(name, touchpad);

        if ((touch || tabletTool) && !denied) {
            reportLines.append(QStringLiteral("%1  %2  R=%3 (%4)  sys=%5")
                                   .arg(name, id)
                                   .arg(r)
                                   .arg(orientationName(r), sys));
        }

        if (!haveFinger && touch && !denied) {
            haveFinger = true;
            m_fingerValue = QStringLiteral("R=%1 (%2)").arg(r).arg(orientationName(r));
            m_fingerDetail = QStringLiteral("%1\n%2").arg(name, id);
            m_fingerSource = QStringLiteral("KWin InputDevice GetAll (name + VID:PID; %1 is not identity)")
                                 .arg(sys);
        }
        if (!havePen && tabletTool && !denied) {
            havePen = true;
            m_penValue = QStringLiteral("R=%1 (%2)").arg(r).arg(orientationName(r));
            m_penDetail = QStringLiteral("%1\n%2").arg(name, id);
            m_penSource = QStringLiteral("KWin InputDevice GetAll (name + VID:PID; %1 is not identity)")
                              .arg(sys);
        }
    }

    m_reportDevices = reportLines.join(QLatin1Char('\n'));
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
        if (name.startsWith(QLatin1String("eDP"))) {
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
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        m_pictureError = err.isEmpty() ? QStringLiteral("kscreen-doctor failed") : err;
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
    m_pendingTransform = QStringLiteral("T=%1").arg(m_outputTransform);
    startCountdown();
    refresh();
}

void ClinicModel::keepPicture()
{
    if (!m_pendingRevert)
        return;
    stopCountdown();
    emit changed();
}

void ClinicModel::revertPicture()
{
    if (!m_pendingRevert)
        return;
    const QString target = m_revertKscreen;
    stopCountdown();
    if (target.isEmpty()) {
        refresh();
        return;
    }
    if (m_outputName.isEmpty())
        m_outputName = QStringLiteral("eDP-1");
    runDoctor(m_outputName, target);
    refresh();
}

void ClinicModel::startCountdown()
{
    m_pendingRevert = true;
    m_revertSecondsLeft = 10;
    m_revertTimer->start();
}

void ClinicModel::stopCountdown()
{
    m_revertTimer->stop();
    m_pendingRevert = false;
    m_revertSecondsLeft = 0;
    m_pendingTransform.clear();
}

void ClinicModel::onRevertTick()
{
    m_revertSecondsLeft--;
    if (m_revertSecondsLeft <= 0) {
        revertPicture();
        return;
    }
    emit changed();
}
