#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class ClinicModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString pictureValue READ pictureValue NOTIFY changed)
    Q_PROPERTY(QString pictureDetail READ pictureDetail NOTIFY changed)
    Q_PROPERTY(QString pictureSource READ pictureSource NOTIFY changed)
    Q_PROPERTY(QString pictureBackend READ pictureBackend NOTIFY changed)

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

    Q_PROPERTY(QString reportText READ reportText NOTIFY changed)

public:
    explicit ClinicModel(QObject *parent = nullptr);

    QString pictureValue() const { return m_pictureValue; }
    QString pictureDetail() const { return m_pictureDetail; }
    QString pictureSource() const { return m_pictureSource; }
    QString pictureBackend() const { return m_pictureBackend; }

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

    QString reportText() const { return m_reportText; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void copyReport();

signals:
    void changed();

private:
    void probeDmi();
    void probeDrm();
    void probeOutputTransform();
    void probeKwinInputs();
    void buildReport();

    QString m_dmiVendor;
    QString m_dmiProduct;
    QString m_dmiBoard;
    QString m_panelOrientation;
    QString m_outputTransform;
    QString m_reportDevices;

    QString m_pictureValue;
    QString m_pictureDetail;
    QString m_pictureSource;
    QString m_pictureBackend = QStringLiteral("later (Phase 2)");

    QString m_fingerValue;
    QString m_fingerDetail;
    QString m_fingerSource;
    QString m_fingerBackend = QStringLiteral("later (Phase 3)");

    QString m_penValue;
    QString m_penDetail;
    QString m_penSource;
    QString m_penBackend = QStringLiteral("later (Phase 3)");

    QString m_arrowValue = QStringLiteral("not inverted / not probed");
    QString m_arrowDetail = QStringLiteral("Cursor plane is Phase 6. This chassis has not shown an inverted arrow.");
    QString m_arrowSource = QStringLiteral("not probed");
    QString m_arrowBackend = QStringLiteral("no");

    QString m_reportText;
};
