#pragma once

#include "backend.h"

class QSocketNotifier;

namespace TiltBack {

class X11Backend final : public OrientationBackend
{
    Q_OBJECT

public:
    explicit X11Backend(QObject *parent = nullptr);
    ~X11Backend() override;

    bool isOpen() const { return m_dpy != nullptr; }

    QString id() const override { return QStringLiteral("x11"); }
    QString pictureBackendLabel() const override { return QStringLiteral("yes (RandR)"); }
    QString inputBackendLabel() const override { return QStringLiteral("yes (XInput CTM)"); }
    QString persistHow() const override;
    QString pictureSource(const OutputInfo &out) const override;
    QString inputSource(const Digitizer &dev) const override;
    bool canChangePicture() const override { return isOpen(); }
    bool canChangeInput() const override { return isOpen(); }
    bool persistKcminput() const override { return false; }

    OutputInfo readOutput() override { return peekOutput(); }
    bool setOutput(const QString &kscreen, QString *error) override;
    QVector<Digitizer> listDigitizers() override;
    Digitizer resolve(DigitizerClass kind, const Digitizer *identity) override;
    bool setResidual(const Digitizer &dev, int r, QString *error) override;
    int getResidual(const Digitizer &dev, bool *ok) override;
    bool stampFollow(const HomeProfile &home, QString *error) override;
    void watchPose() override;

private slots:
    void onX11Readable();

private:
    struct XiDev {
        Digitizer digitizer;
        int deviceId = -1;
        bool touch = false;
        bool stylus = false;
        bool eraser = false;
        bool hasWacomRotation = false;
    };

    QVector<XiDev> scanDevices() const;
    XiDev findDevice(const Digitizer &identity) const;
    bool writeResidual(const XiDev &dev, int r, QString *error);
    bool writeErasers(int r, QString *error);
    OutputInfo peekOutput() const;
    int residualAt(int deviceId, bool hasWacom, bool *ok) const;
    bool readCtm(int deviceId, float *out) const;
    bool writeCtm(int deviceId, const float *ctm);
    bool readWacomRotation(int deviceId, int *value) const;
    bool writeWacomRotation(int deviceId, int value);
    bool readVidPid(int deviceId, quint32 *vendor, quint32 *product) const;
    bool hasProperty(int deviceId, const char *name) const;
    void bindScreens();

    void *m_dpy = nullptr;
    int m_rrEventBase = 0;
    QSocketNotifier *m_notifier = nullptr;
    bool m_watching = false;
};

} // namespace TiltBack
