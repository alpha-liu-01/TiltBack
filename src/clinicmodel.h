#pragma once

#include "backend.h"
#include "kwininput.h"
#include "tiltprobe.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QTimer;

class ClinicModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString pictureValue READ pictureValue NOTIFY changed)
    Q_PROPERTY(QString pictureDetail READ pictureDetail NOTIFY changed)
    Q_PROPERTY(QString pictureSource READ pictureSource NOTIFY changed)
    Q_PROPERTY(QString pictureBackend READ pictureBackend NOTIFY changed)
    Q_PROPERTY(QString pictureError READ pictureError NOTIFY changed)

    Q_PROPERTY(QString fingerValue READ fingerValue NOTIFY changed)
    Q_PROPERTY(QString fingerDetail READ fingerDetail NOTIFY changed)
    Q_PROPERTY(QString fingerSource READ fingerSource NOTIFY changed)
    Q_PROPERTY(QString fingerBackend READ fingerBackend NOTIFY changed)

    Q_PROPERTY(QString penValue READ penValue NOTIFY changed)
    Q_PROPERTY(QString penDetail READ penDetail NOTIFY changed)
    Q_PROPERTY(QString penSource READ penSource NOTIFY changed)
    Q_PROPERTY(QString penBackend READ penBackend NOTIFY changed)

    Q_PROPERTY(QString arrowValue READ arrowValue NOTIFY changed)
    Q_PROPERTY(QString arrowDetail READ arrowDetail NOTIFY changed)
    Q_PROPERTY(QString arrowSource READ arrowSource NOTIFY changed)
    Q_PROPERTY(QString arrowBackend READ arrowBackend NOTIFY changed)

    Q_PROPERTY(QString tiltValue READ tiltValue NOTIFY changed)
    Q_PROPERTY(QString tiltDetail READ tiltDetail NOTIFY changed)
    Q_PROPERTY(QString tiltSource READ tiltSource NOTIFY changed)
    Q_PROPERTY(QString tiltBackend READ tiltBackend NOTIFY changed)

    Q_PROPERTY(QString reportText READ reportText NOTIFY changed)
    Q_PROPERTY(QString homeLine READ homeLine NOTIFY changed)
    Q_PROPERTY(QString persistLine READ persistLine NOTIFY changed)
    Q_PROPERTY(QString persistHow READ persistHow NOTIFY changed)
    Q_PROPERTY(QString followStatus READ followStatus NOTIFY changed)

    Q_PROPERTY(bool pendingPictureRevert READ pendingPictureRevert NOTIFY changed)
    Q_PROPERTY(int pictureSecondsLeft READ pictureSecondsLeft NOTIFY changed)
    Q_PROPERTY(QString pendingPictureTransform READ pendingPictureTransform NOTIFY changed)

    Q_PROPERTY(bool pendingFingerRevert READ pendingFingerRevert NOTIFY changed)
    Q_PROPERTY(int fingerSecondsLeft READ fingerSecondsLeft NOTIFY changed)
    Q_PROPERTY(QString pendingFingerTransform READ pendingFingerTransform NOTIFY changed)

    Q_PROPERTY(bool pendingPenRevert READ pendingPenRevert NOTIFY changed)
    Q_PROPERTY(int penSecondsLeft READ penSecondsLeft NOTIFY changed)
    Q_PROPERTY(QString pendingPenTransform READ pendingPenTransform NOTIFY changed)

public:
    explicit ClinicModel(QObject *parent = nullptr);

    QString pictureValue() const { return m_pictureValue; }
    QString pictureDetail() const { return m_pictureDetail; }
    QString pictureSource() const { return m_pictureSource; }
    QString pictureBackend() const { return m_pictureBackend; }
    QString pictureError() const { return m_pictureError; }

    QString fingerValue() const { return m_fingerValue; }
    QString fingerDetail() const { return m_fingerDetail; }
    QString fingerSource() const { return m_fingerSource; }
    QString fingerBackend() const { return m_fingerBackend; }

    QString penValue() const { return m_penValue; }
    QString penDetail() const { return m_penDetail; }
    QString penSource() const { return m_penSource; }
    QString penBackend() const { return m_penBackend; }

    QString arrowValue() const { return m_arrowValue; }
    QString arrowDetail() const { return m_arrowDetail; }
    QString arrowSource() const { return m_arrowSource; }
    QString arrowBackend() const { return m_arrowBackend; }

    QString tiltValue() const { return m_tiltValue; }
    QString tiltDetail() const { return m_tiltDetail; }
    QString tiltSource() const { return m_tiltSource; }
    QString tiltBackend() const { return m_tiltBackend; }

    QString reportText() const { return m_reportText; }
    QString homeLine() const { return m_homeLine; }
    QString persistLine() const { return m_persistLine; }
    QString persistHow() const { return m_persistHow; }
    QString followStatus() const { return m_followStatus; }

    bool pendingPictureRevert() const { return m_picturePending; }
    int pictureSecondsLeft() const { return m_pictureSeconds; }
    QString pendingPictureTransform() const { return m_picturePendingLabel; }

    bool pendingFingerRevert() const { return m_fingerPending; }
    int fingerSecondsLeft() const { return m_fingerSeconds; }
    QString pendingFingerTransform() const { return m_fingerPendingLabel; }

    bool pendingPenRevert() const { return m_penPending; }
    int penSecondsLeft() const { return m_penSeconds; }
    QString pendingPenTransform() const { return m_penPendingLabel; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void copyReport();
    Q_INVOKABLE void applyPicture(const QString &kscreen);
    Q_INVOKABLE void keepPicture();
    Q_INVOKABLE void revertPicture();
    Q_INVOKABLE void applyFinger(int r);
    Q_INVOKABLE void keepFinger();
    Q_INVOKABLE void revertFinger();
    Q_INVOKABLE void applyPen(int r);
    Q_INVOKABLE void keepPen();
    Q_INVOKABLE void revertPen();
    Q_INVOKABLE void saveHome();
    Q_INVOKABLE void installFollow();

signals:
    void changed();

private slots:
    void onSensorProxyChanged();

private:
    using Digitizer = TiltBack::Digitizer;
    using DigitizerClass = TiltBack::DigitizerClass;

    void probeDmi();
    void probeDrm();
    void probeOutputTransform();
    void probeInputs();
    void probeTilt();
    void bindTiltSignals();
    void buildReport();
    void fillPictureCard();
    void fillFingerCard();
    void fillPenCard();
    void fillTiltCard();
    bool anyPending() const;
    void ensureTimer();
    void startPictureCountdown();
    void stopPictureCountdown();
    void startFingerCountdown();
    void stopFingerCountdown();
    void startPenCountdown();
    void stopPenCountdown();
    void onRevertTick();
    bool resolveDigitizer(DigitizerClass kind, Digitizer *out);
    void applyDigitizer(DigitizerClass kind, int r);
    void warnIfFollowFight(DigitizerClass kind);
    void syncClinicHold();
    void loadHomeState();
    void refreshFollowStatus();

    TiltBack::OrientationBackend *m_backend = nullptr;
    QTimer *m_revertTimer = nullptr;

    QString m_dmiVendor;
    QString m_dmiProduct;
    QString m_dmiBoard;
    QString m_panelOrientation;
    QString m_outputTransform;
    QString m_persistedTransform;
    QString m_outputName;
    QString m_liveKscreen;
    QString m_pictureError;
    QString m_fingerError;
    QString m_penError;
    QString m_reportDevices;

    QString m_pictureValue;
    QString m_pictureDetail;
    QString m_pictureSource;
    QString m_pictureBackend = QStringLiteral("no");

    QString m_fingerValue;
    QString m_fingerDetail;
    QString m_fingerSource;
    QString m_fingerBackend = QStringLiteral("no");

    QString m_penValue;
    QString m_penDetail;
    QString m_penSource;
    QString m_penBackend = QStringLiteral("no");

    QString m_arrowValue = QStringLiteral("not inverted / not probed");
    QString m_arrowDetail = QStringLiteral("Cursor plane is Phase 6. This chassis has not shown an inverted arrow.");
    QString m_arrowSource = QStringLiteral("not probed");
    QString m_arrowBackend = QStringLiteral("no");

    QString m_tiltValue = QStringLiteral("no sensor");
    QString m_tiltDetail;
    QString m_tiltSource = QStringLiteral("sysfs + udev + SensorProxy");
    QString m_tiltBackend = QStringLiteral("sysfs + udev + SensorProxy");
    TiltBack::TiltFact m_tilt;

    QString m_reportText;
    QString m_homeLine;
    QString m_persistLine;
    QString m_persistHow;
    QString m_followStatus;
    TiltBack::HomeProfile m_home;

    Digitizer m_finger;
    Digitizer m_pen;

    bool m_picturePending = false;
    int m_pictureSeconds = 0;
    QString m_picturePendingLabel;
    QString m_revertKscreen;

    bool m_fingerPending = false;
    int m_fingerSeconds = 0;
    QString m_fingerPendingLabel;
    int m_fingerRevertR = 0;
    int m_fingerAppliedR = 0;

    bool m_penPending = false;
    int m_penSeconds = 0;
    QString m_penPendingLabel;
    int m_penRevertR = 0;
    int m_penAppliedR = 0;
};
