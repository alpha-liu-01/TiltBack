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

private:
    void resolveTargets();
    void schedule(const QString &reason);
    void stamp(const QString &reason);
    void reloadHomeIfChanged();

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
};

} // namespace TiltBack
