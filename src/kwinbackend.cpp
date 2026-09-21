#include "kwinbackend.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>

namespace TiltBack {
namespace {

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

} // namespace

KwinBackend::KwinBackend(QObject *parent)
    : OrientationBackend(parent)
    , m_tick(new QTimer(this))
{
    m_tick->setInterval(400);
    connect(m_tick, &QTimer::timeout, this, &KwinBackend::onTick);
}

QString KwinBackend::pictureSource(const OutputInfo &out) const
{
    if (out.name.isEmpty())
        return QStringLiteral("KScreen getConfig (live) + DMI + DRM");
    return QStringLiteral("KScreen getConfig (live, %1) + DMI + DRM").arg(out.name);
}

QString KwinBackend::inputSource(const Digitizer &dev) const
{
    return QStringLiteral("KWin InputDevice GetAll (name + VID:PID; %1 is not identity)")
        .arg(dev.sysName);
}

bool KwinBackend::ensureKscreenBackend()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusMessage ping = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KScreen"), QStringLiteral("/backend"),
        QStringLiteral("org.kde.kscreen.Backend"), QStringLiteral("getConfig"));
    const QDBusMessage pingReply = bus.call(ping);
    if (pingReply.type() != QDBusMessage::ErrorMessage)
        return true;
    QDBusMessage req = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KScreen"), QStringLiteral("/"),
        QStringLiteral("org.kde.KScreen"), QStringLiteral("requestBackend"));
    req << QString() << QVariantMap();
    const QDBusMessage reqReply = bus.call(req);
    if (reqReply.type() == QDBusMessage::ErrorMessage)
        return false;
    if (!reqReply.arguments().isEmpty() && !reqReply.arguments().at(0).toBool())
        return false;
    return true;
}

OutputInfo KwinBackend::readOutput()
{
    OutputInfo info;
    if (!ensureKscreenBackend())
        return info;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KScreen"), QStringLiteral("/backend"),
        QStringLiteral("org.kde.kscreen.Backend"), QStringLiteral("getConfig"));
    const QDBusMessage reply = bus.call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return info;
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
        return info;
    info.name = unwrap(chosen.value(QStringLiteral("name"))).toString();
    const int rotation = unwrap(chosen.value(QStringLiteral("rotation"))).toInt();
    info.tKscreen = kscreenNameFromRotation(rotation);
    info.tKwin = kwinNameFromKscreen(info.tKscreen);
    info.ok = !info.tKscreen.isEmpty();
    return info;
}

bool KwinBackend::setOutput(const QString &kscreen, QString *error)
{
    const QString want = normalizeKscreen(kscreen);
    if (want.isEmpty()) {
        if (error)
            *error = QStringLiteral("unknown transform %1").arg(kscreen);
        return false;
    }
    OutputInfo live = readOutput();
    QString name = live.name;
    if (name.isEmpty())
        name = QStringLiteral("eDP-1");
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (bin.isEmpty()) {
        if (error)
            *error = QStringLiteral("kscreen-doctor not found");
        return false;
    }
    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments({QStringLiteral("output.%1.rotation.%2").arg(name, want)});
    proc.start();
    if (!proc.waitForFinished(15000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("kscreen-doctor timed out");
        return false;
    }
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString combined = err.isEmpty() ? out : err;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0
        || combined.contains(QLatin1String("not found"), Qt::CaseInsensitive)) {
        if (error)
            *error = combined.isEmpty() ? QStringLiteral("kscreen-doctor failed") : combined;
        return false;
    }
    return true;
}

QVector<Digitizer> KwinBackend::listDigitizers()
{
    return listKwinDigitizers();
}

Digitizer KwinBackend::resolve(DigitizerClass kind, const Digitizer *identity)
{
    return resolveDigitizer(kind, identity);
}

bool KwinBackend::setResidual(const Digitizer &dev, int r, QString *error)
{
    return setOrientation(dev.path, r, error);
}

int KwinBackend::getResidual(const Digitizer &dev, bool *ok)
{
    return getOrientation(dev.path, ok);
}

bool KwinBackend::stampFollow(const HomeProfile &home, QString *error)
{
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
    Digitizer finger = resolveDigitizer(DigitizerClass::Finger, wantF.ok ? &wantF : nullptr);
    Digitizer pen = resolveDigitizer(DigitizerClass::Pen, wantP.ok ? &wantP : nullptr);
    auto one = [&](const Digitizer &dev, int want) {
        if (!dev.ok)
            return true;
        bool ok = false;
        const int now = getOrientation(dev.path, &ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("orientation Get failed");
            return false;
        }
        if (now == want)
            return true;
        return setOrientation(dev.path, want, error);
    };
    return one(finger, home.rTouch) && one(pen, home.rPen);
}

void KwinBackend::watchPose()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QStringLiteral("org.kde.KWin"), QString(),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this, SIGNAL(poseChanged()));
    bus.connect(QStringLiteral("org.kde.KWin"),
                QStringLiteral("/org/kde/KWin/InputDevice"),
                QStringLiteral("org.kde.KWin.InputDeviceManager"),
                QStringLiteral("deviceAdded"), this, SIGNAL(devicesChanged()));
    bus.connect(QStringLiteral("org.kde.KWin"),
                QStringLiteral("/org/kde/KWin/InputDevice"),
                QStringLiteral("org.kde.KWin.InputDeviceManager"),
                QStringLiteral("deviceRemoved"), this, SIGNAL(devicesChanged()));
    setPoseWatchEnabled(true);
}

void KwinBackend::setPoseWatchEnabled(bool enabled)
{
    if (enabled)
        m_tick->start();
    else
        m_tick->stop();
}

void KwinBackend::onTick()
{
    emit poseChanged();
}

} // namespace TiltBack
