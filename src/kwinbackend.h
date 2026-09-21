#pragma once

#include "backend.h"

class QTimer;

namespace TiltBack {

class KwinBackend final : public OrientationBackend
{
    Q_OBJECT

public:
    explicit KwinBackend(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("kwin"); }
    QString pictureBackendLabel() const override { return QStringLiteral("yes (KScreen session)"); }
    QString inputBackendLabel() const override { return QStringLiteral("yes (KWin session)"); }
    QString pictureSource(const OutputInfo &out) const override;
    QString inputSource(const Digitizer &dev) const override;
    bool canChangePicture() const override { return true; }
    bool canChangeInput() const override { return true; }
    bool persistKcminput() const override { return true; }
    QString persistHow() const override
    {
        return QStringLiteral("home.json + kcminputrc Orientation=; follow restamps R only");
    }

    OutputInfo readOutput() override;
    bool setOutput(const QString &kscreen, QString *error) override;
    QVector<Digitizer> listDigitizers() override;
    Digitizer resolve(DigitizerClass kind, const Digitizer *identity) override;
    bool setResidual(const Digitizer &dev, int r, QString *error) override;
    int getResidual(const Digitizer &dev, bool *ok) override;
    bool stampFollow(const HomeProfile &home, QString *error) override;
    void watchPose() override;

private slots:
    void onTick();

private:
    bool ensureKscreenBackend();
    QTimer *m_tick = nullptr;
};

} // namespace TiltBack
