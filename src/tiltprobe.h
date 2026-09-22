#pragma once

#include <QString>

namespace TiltBack {

enum class TiltHonesty { NoSensor, Unreadable, Readable };

struct TiltFact {
    TiltHonesty honesty = TiltHonesty::NoSensor;
    QString honestyText;
    QString name;
    QString modalias;
    QString extraId;
    QString label;
    QString sysPath;
    QString kernelMatrix;
    QString udevMatrix;
    QString proxyType;
    QString devNode;
    QString devMode;
    bool rawOk = false;
    QString raw;
    QString rawError;
    bool proxyPresent = false;
    bool hasAccelerometer = false;
    QString orientation;
    QString reason;
};

class TiltProbe
{
public:
    static TiltFact probe();
};

} // namespace TiltBack
