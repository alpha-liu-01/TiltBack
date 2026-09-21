#pragma once

#include "backend.h"
#include "kwininput.h"

#include <QDateTime>
#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

namespace TiltBack {

class FollowEngine : public QObject
{
    Q_OBJECT

public:
    explicit FollowEngine(QObject *parent = nullptr);
    int start();

private slots:
    void onDirChanged(const QString &path);
    void onDebounce();
    void onDelayedStamp();
    void onPoseChanged();
    void onDevicesChanged();
    void onPrepareForSleep(bool sleeping);
    void onQuietEnd();

private:
    void resolveTargets();
    void schedule(const QString &reason);
    void stamp(const QString &reason);
    void reloadHomeIfChanged();
    void quietFor(int ms);
    bool isHeld() const;
    void bindSleepSignals();

    OrientationBackend *m_backend = nullptr;
    HomeProfile m_home;
    Digitizer m_finger;
    Digitizer m_pen;
    QString m_reason;
    QString m_homePath;
    QDateTime m_homeMtime;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QTimer *m_delayed = nullptr;
    QTimer *m_quiet = nullptr;
    bool m_asleep = false;
};

} // namespace TiltBack
