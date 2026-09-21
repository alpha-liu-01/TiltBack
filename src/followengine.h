#pragma once

#include "kwininput.h"

#include <QObject>
#include <QString>
#include <QtDBus/QDBusMessage>

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
    void onPropertiesChanged(const QDBusMessage &message);
    void onDeviceChanged();
    void onDirChanged(const QString &path);
    void onDebounce();
    void onTick();
    void onDelayedStamp();

private:
    void resolveTargets();
    void schedule(const QString &reason);
    void stamp(const QString &reason);
    void bindDeviceSignals();

    HomeProfile m_home;
    Digitizer m_finger;
    Digitizer m_pen;
    QString m_reason;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QTimer *m_tick = nullptr;
    QTimer *m_delayed = nullptr;
};

} // namespace TiltBack
