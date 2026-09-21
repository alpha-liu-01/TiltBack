#pragma once

#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

namespace TiltBack {

enum class DigitizerClass { Finger, Pen };

struct Digitizer {
    QString name;
    quint32 vendor = 0;
    quint32 product = 0;
    QString sysName;
    QString path;
    int r = 0;
    bool ok = false;
};

struct HomeProfile {
    QString tHome;
    int rTouch = 8;
    int rPen = 8;
    QString fingerName;
    quint32 fingerVendor = 0;
    quint32 fingerProduct = 0;
    QString penName;
    quint32 penVendor = 0;
    quint32 penProduct = 0;
};

QVariant unwrap(const QVariant &value);
bool isDenied(const QString &name, bool touchpad, quint32 vendor = 0, quint32 product = 0);
QString vidPid(quint32 vendor, quint32 product);
QVector<Digitizer> listKwinDigitizers();
Digitizer resolveDigitizer(DigitizerClass kind, const Digitizer *identity = nullptr);
bool setOrientation(const QString &path, int r, QString *error = nullptr);
int getOrientation(const QString &path, bool *ok = nullptr);

QString homeProfilePath();
HomeProfile w620Home();
HomeProfile loadHome(const QString &dmiProduct, bool allowDmiSeed = true);
bool saveHomeFile(const HomeProfile &home, QString *error);
bool persistKcminputrc(const HomeProfile &home, QString *error);

QString clinicHoldPath();
void writeClinicHold();
void clearClinicHold();
bool clinicHoldActive();

int installFollow(const QString &binaryPath, QString *error);
void requestInstallGreeter();
int installGreeter(QString *error);

} // namespace TiltBack
