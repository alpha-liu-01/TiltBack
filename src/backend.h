#pragma once

#include "kwininput.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace TiltBack {

struct OutputInfo {
    QString name;
    QString tKscreen;
    QString tKwin;
    bool ok = false;
};

class OrientationBackend : public QObject
{
    Q_OBJECT

public:
    explicit OrientationBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual QString id() const = 0;
    virtual QString pictureBackendLabel() const = 0;
    virtual QString inputBackendLabel() const = 0;
    virtual QString pictureSource(const OutputInfo &out) const = 0;
    virtual QString inputSource(const Digitizer &dev) const = 0;
    virtual bool canChangePicture() const = 0;
    virtual bool canChangeInput() const = 0;
    virtual bool persistKcminput() const = 0;
    virtual QString persistHow() const = 0;

    virtual OutputInfo readOutput() = 0;
    virtual bool setOutput(const QString &kscreen, QString *error) = 0;

    virtual QVector<Digitizer> listDigitizers() = 0;
    virtual Digitizer resolve(DigitizerClass kind, const Digitizer *identity = nullptr) = 0;
    virtual bool setResidual(const Digitizer &dev, int r, QString *error) = 0;
    virtual int getResidual(const Digitizer &dev, bool *ok = nullptr) = 0;
    virtual bool stampFollow(const HomeProfile &home, QString *error) = 0;

    virtual void watchPose() = 0;
    virtual void setPoseWatchEnabled(bool enabled) { Q_UNUSED(enabled); }
    virtual bool canSoftwareCursor() const { return false; }
    virtual bool commitOutput(QString *error)
    {
        Q_UNUSED(error);
        return true;
    }
    virtual QString inputBackendLabelFor(DigitizerClass kind) const
    {
        Q_UNUSED(kind);
        return inputBackendLabel();
    }

signals:
    void poseChanged();
    void devicesChanged();
};

bool sessionLooksX11();
bool kwinBusAvailable();
bool mutterBusAvailable();
QString normalizeKscreen(const QString &requested);
QString kwinNameFromKscreen(const QString &kscreen);
bool isBuiltinOutput(const QString &name);
void composeCtm(const QString &tKscreen, int r, float *out);
int classifyCtmResidual(const float *ctm, const QString &tKscreen);
int followResidual(const QString &tNow, const QString &tHome, int rHome);

OrientationBackend *createBackend(QObject *parent);

} // namespace TiltBack
