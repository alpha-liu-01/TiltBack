#pragma once

#include "backend.h"

class QFileSystemWatcher;

namespace TiltBack {

class GnomeBackend final : public OrientationBackend
{
    Q_OBJECT

public:
    explicit GnomeBackend(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("gnome"); }
    QString pictureBackendLabel() const override { return QStringLiteral("yes (Mutter DisplayConfig)"); }
    QString inputBackendLabel() const override { return QStringLiteral("yes (udev constant)"); }
    QString inputBackendLabelFor(DigitizerClass kind) const override;
    QString persistHow() const override
    {
        return QStringLiteral(
            "home.json only; Mutter owns monitors.xml + GSettings; constant udev R if GSettings cannot");
    }
    QString pictureSource(const OutputInfo &out) const override;
    QString inputSource(const Digitizer &dev) const override;
    bool canChangePicture() const override { return true; }
    bool canChangeInput() const override { return true; }
    bool persistKcminput() const override { return false; }

    OutputInfo readOutput() override;
    bool setOutput(const QString &kscreen, QString *error) override;
    bool commitOutput(QString *error) override;
    QVector<Digitizer> listDigitizers() override;
    Digitizer resolve(DigitizerClass kind, const Digitizer *identity) override;
    bool setResidual(const Digitizer &dev, int r, QString *error) override;
    int getResidual(const Digitizer &dev, bool *ok) override;
    bool stampFollow(const HomeProfile &home, QString *error) override;
    void watchPose() override;

private:
    bool applyTransform(const QString &kscreen, bool persistent, QString *error);
    bool applyViaGdctl(const QString &connector, const QString &kscreen, bool persistent, QString *error);
    bool applyViaDisplayConfig(const QString &connector, const QString &kscreen, bool persistent,
                               QString *error);
    void bindBuiltinOutputs();

    QFileSystemWatcher *m_watcher = nullptr;
    QString m_lastKscreen;
    bool m_watching = false;
};

} // namespace TiltBack
