#include "followengine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QTimer>
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
    , m_backend(createBackend(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_debounce(new QTimer(this))
    , m_delayed(new QTimer(this))
{
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &FollowEngine::onDebounce);

    m_delayed->setSingleShot(true);
    m_delayed->setInterval(400);
    connect(m_delayed, &QTimer::timeout, this, &FollowEngine::onDelayedStamp);

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &FollowEngine::onDirChanged);
    connect(m_backend, &OrientationBackend::poseChanged, this, &FollowEngine::onPoseChanged);
    connect(m_backend, &OrientationBackend::devicesChanged, this, &FollowEngine::onDevicesChanged);
}

int FollowEngine::start()
{
    const QString product = readSysfs(QStringLiteral("/sys/class/dmi/id/product_name"));
    m_home = loadHome(product);
    if (m_home.tHome.isEmpty()) {
        logLine(QStringLiteral("follow: no home.json — save home first"));
        return 1;
    }

    m_homePath = homeProfilePath();
    m_homeMtime = QFileInfo(m_homePath).lastModified();
    QStringList watchDirs = {
        QDir::home().filePath(QStringLiteral(".config")),
        QDir::home().filePath(QStringLiteral(".local/share/kscreen")),
        QFileInfo(m_homePath).absolutePath(),
    };
    for (const QString &dir : watchDirs) {
        if (QDir(dir).exists())
            m_watcher->addPath(dir);
    }

    resolveTargets();
    if (!m_finger.ok)
        logLine(QStringLiteral("follow: finger not resolved"));
    if (!m_pen.ok)
        logLine(QStringLiteral("follow: pen not resolved"));
    m_backend->watchPose();
    stamp(QStringLiteral("startup"));
    QTimer::singleShot(2000, this, [this]() { stamp(QStringLiteral("startup-retry")); });
    logLine(QStringLiteral("follow idle (%1) R_touch=%2 R_pen=%3")
                .arg(m_backend->id())
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
    m_finger = m_backend->resolve(DigitizerClass::Finger, wantFinger.ok ? &wantFinger : nullptr);
    m_pen = m_backend->resolve(DigitizerClass::Pen, wantPen.ok ? &wantPen : nullptr);
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
    QString err;
    if (!m_backend->stampFollow(m_home, &err))
        logLine(QStringLiteral("follow %1 stamp failed: %2").arg(reason, err));
}

void FollowEngine::onPoseChanged()
{
    schedule(QStringLiteral("pose"));
}

void FollowEngine::onDevicesChanged()
{
    resolveTargets();
    schedule(QStringLiteral("device list"));
}

void FollowEngine::onDirChanged(const QString &path)
{
    reloadHomeIfChanged();
    if (path.endsWith(QLatin1String("kscreen")) || path.endsWith(QLatin1String(".config"))) {
        schedule(QStringLiteral("output config"));
        m_delayed->start();
    }
}

void FollowEngine::onDebounce()
{
    stamp(m_reason.isEmpty() ? QStringLiteral("debounce") : m_reason);
}

void FollowEngine::reloadHomeIfChanged()
{
    const QString path = homeProfilePath();
    const QFileInfo info(path);
    const QDateTime mtime = info.lastModified();
    if (path == m_homePath && mtime == m_homeMtime)
        return;
    m_homePath = path;
    m_homeMtime = mtime;
    if (!info.exists())
        return;
    const QString product = readSysfs(QStringLiteral("/sys/class/dmi/id/product_name"));
    HomeProfile next = loadHome(product);
    if (next.tHome.isEmpty())
        return;
    const bool same = next.rTouch == m_home.rTouch && next.rPen == m_home.rPen
        && next.fingerName == m_home.fingerName && next.penName == m_home.penName
        && next.fingerVendor == m_home.fingerVendor && next.fingerProduct == m_home.fingerProduct
        && next.penVendor == m_home.penVendor && next.penProduct == m_home.penProduct;
    m_home = next;
    if (same)
        return;
    logLine(QStringLiteral("follow reloaded home.json R_touch=%1 R_pen=%2")
                .arg(m_home.rTouch)
                .arg(m_home.rPen));
    resolveTargets();
    stamp(QStringLiteral("home.json"));
}

void FollowEngine::onDelayedStamp()
{
    stamp(QStringLiteral("output config delayed"));
}

} // namespace TiltBack
