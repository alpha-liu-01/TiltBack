#include "followengine.h"

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <cstdio>

namespace TiltBack {
namespace {

QString readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

void logLine(const QString &line)
{
    const QByteArray msg = line.toUtf8() + '\n';
    std::fwrite(msg.constData(), 1, static_cast<size_t>(msg.size()), stderr);
    std::fflush(stderr);
    QFile f(QStringLiteral("/tmp/tiltback-follow.log"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        f.write(msg);
}

} // namespace

FollowEngine::FollowEngine(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_debounce(new QTimer(this))
    , m_tick(new QTimer(this))
    , m_delayed(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &FollowEngine::onDebounce);

    m_tick->setInterval(400);
    connect(m_tick, &QTimer::timeout, this, &FollowEngine::onTick);

    m_delayed->setSingleShot(true);
    m_delayed->setInterval(400);
    connect(m_delayed, &QTimer::timeout, this, &FollowEngine::onDelayedStamp);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &FollowEngine::onDirChanged);
}

int FollowEngine::start()
{
    const QString product = readSysfs(QStringLiteral("/sys/class/dmi/id/product_name"));
    m_home = loadHome(product);
    if (m_home.tHome.isEmpty())
        m_home = w620Home();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        logLine(QStringLiteral("follow: no session bus"));
        return 1;
    }

    bus.connect(QStringLiteral("org.kde.KWin"), QString(),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QDBusMessage)));
    bus.connect(QStringLiteral("org.kde.KWin"),
                QStringLiteral("/org/kde/KWin/InputDevice"),
                QStringLiteral("org.kde.KWin.InputDeviceManager"),
                QStringLiteral("deviceAdded"), this, SLOT(onDeviceChanged()));
    bus.connect(QStringLiteral("org.kde.KWin"),
                QStringLiteral("/org/kde/KWin/InputDevice"),
                QStringLiteral("org.kde.KWin.InputDeviceManager"),
                QStringLiteral("deviceRemoved"), this, SLOT(onDeviceChanged()));

    for (const QString &dir : {
             QDir::home().filePath(QStringLiteral(".config")),
             QDir::home().filePath(QStringLiteral(".local/share/kscreen")),
         }) {
        if (QDir(dir).exists())
            m_watcher->addPath(dir);
    }

    resolveTargets();
    if (!m_finger.ok)
        logLine(QStringLiteral("follow: finger not resolved"));
    if (!m_pen.ok)
        logLine(QStringLiteral("follow: pen not resolved"));
    stamp(QStringLiteral("startup"));
    QTimer::singleShot(2000, this, [this]() { stamp(QStringLiteral("startup-retry")); });
    m_tick->start();
    logLine(QStringLiteral("follow idle (D-Bus + dir watch + 0.4s Get tick) R_touch=%1 R_pen=%2")
                .arg(m_home.rTouch)
                .arg(m_home.rPen));
    return 0;
}

void FollowEngine::resolveTargets()
{
    Digitizer wantFinger;
    wantFinger.name = m_home.fingerName;
    wantFinger.vendor = m_home.fingerVendor;
    wantFinger.product = m_home.fingerProduct;
    wantFinger.ok = !m_home.fingerName.isEmpty();
    Digitizer wantPen;
    wantPen.name = m_home.penName;
    wantPen.vendor = m_home.penVendor;
    wantPen.product = m_home.penProduct;
    wantPen.ok = !m_home.penName.isEmpty();
    m_finger = resolveDigitizer(DigitizerClass::Finger, wantFinger.ok ? &wantFinger : nullptr);
    m_pen = resolveDigitizer(DigitizerClass::Pen, wantPen.ok ? &wantPen : nullptr);
    bindDeviceSignals();
}

void FollowEngine::bindDeviceSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_finger.ok) {
        bus.connect(QStringLiteral("org.kde.KWin"), m_finger.path,
                    QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"), this,
                    SLOT(onPropertiesChanged(QDBusMessage)));
    }
    if (m_pen.ok) {
        bus.connect(QStringLiteral("org.kde.KWin"), m_pen.path,
                    QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"), this,
                    SLOT(onPropertiesChanged(QDBusMessage)));
    }
}

void FollowEngine::schedule(const QString &reason)
{
    m_reason = reason;
    m_debounce->start();
}

void FollowEngine::stamp(const QString &reason)
{
    if (clinicHoldActive())
        return;
    if (!m_finger.ok || !m_pen.ok)
        resolveTargets();

    auto one = [&](Digitizer *dev, int want) {
        if (!dev->ok || dev->path.isEmpty())
            return;
        bool ok = false;
        const int live = getOrientation(dev->path, &ok);
        if (!ok) {
            resolveTargets();
            if (!dev->ok)
                return;
        }
        const int now = ok ? live : getOrientation(dev->path, &ok);
        if (!ok || now == want)
            return;
        QString err;
        if (setOrientation(dev->path, want, &err))
            logLine(QStringLiteral("follow %1 %2 R=%3 -> %4").arg(reason, dev->name).arg(now).arg(want));
        else
            logLine(QStringLiteral("follow set %1 failed: %2").arg(dev->name, err));
    };
    one(&m_finger, m_home.rTouch);
    one(&m_pen, m_home.rPen);
}

void FollowEngine::onPropertiesChanged(const QDBusMessage &message)
{
    const QString path = message.path();
    if (!path.startsWith(QLatin1String("/org/kde/KWin/InputDevice/")))
        return;
    if (path == QLatin1String("/org/kde/KWin/InputDevice"))
        return;
    schedule(QStringLiteral("kwin property"));
}

void FollowEngine::onDeviceChanged()
{
    resolveTargets();
    schedule(QStringLiteral("device list"));
}

void FollowEngine::onDirChanged(const QString &path)
{
    if (path.endsWith(QLatin1String("kscreen")) || path.endsWith(QLatin1String(".config"))) {
        schedule(QStringLiteral("output config"));
        m_delayed->start();
    }
}

void FollowEngine::onDebounce()
{
    stamp(m_reason.isEmpty() ? QStringLiteral("debounce") : m_reason);
}

void FollowEngine::onTick()
{
    stamp(QStringLiteral("tick"));
}

void FollowEngine::onDelayedStamp()
{
    stamp(QStringLiteral("output config delayed"));
}

} // namespace TiltBack
