#include "clinicmodel.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <QtDBus/QDBusConnection>

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace {

QString readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString orientationName(int value)
{
    switch (value) {
    case 1:
        return QStringLiteral("Portrait");
    case 2:
        return QStringLiteral("Landscape");
    case 4:
        return QStringLiteral("InvertedPortrait");
    case 8:
        return QStringLiteral("InvertedLandscape");
    case 0:
        return QStringLiteral("Primary");
    default:
        return QStringLiteral("raw:%1").arg(value);
    }
}

QString panelOrientationName(int value)
{
    switch (value) {
    case -1:
        return QStringLiteral("UNKNOWN");
    case 0:
        return QStringLiteral("NORMAL");
    case 1:
        return QStringLiteral("BOTTOM_UP");
    case 2:
        return QStringLiteral("LEFT_UP");
    case 3:
        return QStringLiteral("RIGHT_UP");
    default:
        return QStringLiteral("raw:%1").arg(value);
    }
}

using TiltBack::isBuiltinOutput;
using TiltBack::vidPid;

QString findEdpTransform(const QJsonValue &value, QString *fromName)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();
        const QString transform = obj.value(QStringLiteral("transform")).toString();
        if (!transform.isEmpty() && (isBuiltinOutput(name) || name.isEmpty())) {
            if (fromName && !name.isEmpty())
                *fromName = name;
            if (isBuiltinOutput(name))
                return transform;
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString found = findEdpTransform(it.value(), fromName);
            if (!found.isEmpty())
                return found;
        }
        if (!transform.isEmpty()) {
            if (fromName && !name.isEmpty())
                *fromName = name;
            return transform;
        }
    } else if (value.isArray()) {
        QString fallback;
        QString fallbackName;
        for (const QJsonValue &item : value.toArray()) {
            QString name;
            const QString found = findEdpTransform(item, &name);
            if (found.isEmpty())
                continue;
            if (isBuiltinOutput(name)) {
                if (fromName)
                    *fromName = name;
                return found;
            }
            if (fallback.isEmpty()) {
                fallback = found;
                fallbackName = name;
            }
        }
        if (!fallback.isEmpty()) {
            if (fromName)
                *fromName = fallbackName;
            return fallback;
        }
    }
    return {};
}

} // namespace

ClinicModel::ClinicModel(QObject *parent)
    : QObject(parent)
    , m_backend(TiltBack::createBackend(this))
    , m_revertTimer(new QTimer(this))
{
    m_revertTimer->setInterval(1000);
    connect(m_revertTimer, &QTimer::timeout, this, &ClinicModel::onRevertTick);
    m_pictureBackend = m_backend->pictureBackendLabel();
    m_fingerBackend = m_backend->inputBackendLabelFor(TiltBack::DigitizerClass::Finger);
    m_penBackend = m_backend->inputBackendLabelFor(TiltBack::DigitizerClass::Pen);
    m_persistHow = m_backend->persistHow();
    if (m_backend->id() == QLatin1String("x11")) {
        m_arrowDetail = QStringLiteral(
            "SWCursor needs an Xorg restart. This chassis has not shown an inverted sprite.");
        m_arrowSource = QStringLiteral("diagnose only");
    }
    bindTiltSignals();
    TiltBack::loadTiltHolds(&m_tiltHolds, nullptr);
    refresh();
}

void ClinicModel::refresh()
{
    m_pictureError.clear();
    m_fingerError.clear();
    m_penError.clear();
    if (!m_tiltPending)
        m_tiltError.clear();
    probeDmi();
    probeDrm();
    probeOutputTransform();
    probeInputs();
    probeTilt();
    loadHomeState();
    refreshFollowStatus();
    buildReport();
    emit changed();
}

void ClinicModel::copyReport()
{
    if (QClipboard *clip = QGuiApplication::clipboard())
        clip->setText(m_reportText);
}

void ClinicModel::probeDmi()
{
    m_dmiVendor = readSysfs(QStringLiteral("/sys/class/dmi/id/sys_vendor"));
    m_dmiProduct = readSysfs(QStringLiteral("/sys/class/dmi/id/product_name"));
    m_dmiBoard = readSysfs(QStringLiteral("/sys/class/dmi/id/board_name"));
}

void ClinicModel::probeDrm()
{
    m_panelOrientation.clear();
    const QDir dri(QStringLiteral("/dev/dri"));
    const QStringList cards = dri.entryList(QStringList() << QStringLiteral("card*"), QDir::System);
    for (const QString &card : cards) {
        if (card.startsWith(QLatin1String("card")) && card.contains(QLatin1Char('-')))
            continue;
        const QByteArray path = dri.absoluteFilePath(card).toUtf8();
        const int fd = open(path.constData(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        drmModeRes *res = drmModeGetResources(fd);
        if (!res) {
            close(fd);
            continue;
        }
        for (int i = 0; i < res->count_connectors; ++i) {
            drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
            if (!conn)
                continue;
            const bool edp = conn->connector_type == DRM_MODE_CONNECTOR_eDP
                || conn->connector_type == DRM_MODE_CONNECTOR_LVDS
                || conn->connector_type == DRM_MODE_CONNECTOR_DSI;
            if (!edp) {
                drmModeFreeConnector(conn);
                continue;
            }
            drmModeObjectProperties *props = drmModeObjectGetProperties(
                fd, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR);
            if (props) {
                for (uint32_t p = 0; p < props->count_props; ++p) {
                    drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[p]);
                    if (!prop)
                        continue;
                    if (QByteArray(prop->name) == "panel orientation") {
                        const int raw = static_cast<int>(props->prop_values[p]);
                        m_panelOrientation = QStringLiteral("%1 (%2)")
                                                 .arg(panelOrientationName(raw))
                                                 .arg(raw);
                    }
                    drmModeFreeProperty(prop);
                }
                drmModeFreeObjectProperties(props);
            }
            drmModeFreeConnector(conn);
            if (!m_panelOrientation.isEmpty())
                break;
        }
        drmModeFreeResources(res);
        close(fd);
        if (!m_panelOrientation.isEmpty())
            break;
    }
    if (m_panelOrientation.isEmpty())
        m_panelOrientation = QStringLiteral("unavailable");
}

void ClinicModel::probeOutputTransform()
{
    m_persistedTransform.clear();
    const QString path = QDir::home().filePath(QStringLiteral(".config/kwinoutputconfig.json"));
    QFile f(path);
    QString fromName;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        m_persistedTransform = findEdpTransform(root, &fromName);
    }

    const TiltBack::OutputInfo live = m_backend->readOutput();
    if (live.ok) {
        m_outputName = live.name;
        m_liveKscreen = live.tKscreen;
        m_outputTransform = live.tKwin;
        m_pictureSource = m_backend->pictureSource(live);
    } else {
        if (m_outputTransform.isEmpty())
            m_outputTransform = m_persistedTransform.isEmpty()
                ? QStringLiteral("unknown")
                : m_persistedTransform;
        if (m_pictureError.isEmpty()) {
            m_pictureError = m_backend->canChangePicture()
                ? QStringLiteral("output read failed")
                : QStringLiteral("backend cannot change");
        }
        m_pictureSource = m_backend->pictureSource(live);
    }
    m_pictureBackend = m_backend->pictureBackendLabel();
    if (!fromName.isEmpty() && m_outputName.isEmpty())
        m_outputName = fromName;
    fillPictureCard();
}

void ClinicModel::fillPictureCard()
{
    m_pictureValue = QStringLiteral("T=%1").arg(m_outputTransform);
    QString detail = QStringLiteral("%1 %2\nboard %3\npanel-orientation %4")
                         .arg(m_dmiVendor, m_dmiProduct, m_dmiBoard, m_panelOrientation);
    if (!m_persistedTransform.isEmpty() && m_persistedTransform != m_outputTransform)
        detail += QStringLiteral("\npersisted %1 (file, may be stale)").arg(m_persistedTransform);
    if (!m_pictureError.isEmpty())
        detail += QStringLiteral("\n%1").arg(m_pictureError);
    m_pictureDetail = detail;
    if (!m_persistedTransform.isEmpty() && m_persistedTransform != m_outputTransform
        && !m_pictureSource.contains(QLatin1String("kwinoutputconfig.json"))) {
        m_pictureSource += QStringLiteral(" + persisted kwinoutputconfig.json=%1")
                               .arg(m_persistedTransform);
    }
}

void ClinicModel::probeInputs()
{
    m_fingerValue = QStringLiteral("no touchscreen");
    m_fingerDetail.clear();
    m_penValue = QStringLiteral("no stylus");
    m_penDetail.clear();
    m_reportDevices.clear();
    m_fingerBackend = m_backend->inputBackendLabelFor(DigitizerClass::Finger);
    m_penBackend = m_backend->inputBackendLabelFor(DigitizerClass::Pen);

    const QVector<TiltBack::Digitizer> all = m_backend->listDigitizers();
    if (all.isEmpty() && !m_backend->canChangeInput()) {
        m_fingerValue = QStringLiteral("backend cannot change");
        m_penValue = QStringLiteral("backend cannot change");
        m_fingerSource = m_backend->inputSource({});
        m_penSource = m_fingerSource;
        return;
    }
    QStringList reportLines;
    for (const TiltBack::Digitizer &d : all) {
        reportLines.append(QStringLiteral("%1  %2  R=%3 (%4)  sys=%5")
                               .arg(d.name, vidPid(d.vendor, d.product))
                               .arg(d.r)
                               .arg(orientationName(d.r), d.sysName));
    }
    m_reportDevices = reportLines.join(QLatin1Char('\n'));

    const Digitizer *wantF = m_finger.ok ? &m_finger : nullptr;
    const Digitizer *wantP = m_pen.ok ? &m_pen : nullptr;
    m_finger = m_backend->resolve(DigitizerClass::Finger, wantF);
    m_pen = m_backend->resolve(DigitizerClass::Pen, wantP);
    fillFingerCard();
    fillPenCard();
}

void ClinicModel::fillFingerCard()
{
    m_fingerSource = m_backend->inputSource(m_finger);
    if (!m_finger.ok) {
        if (m_fingerValue.isEmpty())
            m_fingerValue = QStringLiteral("no touchscreen");
        return;
    }
    m_fingerValue = QStringLiteral("R=%1 (%2)").arg(m_finger.r).arg(orientationName(m_finger.r));
    m_fingerDetail = QStringLiteral("%1\n%2").arg(m_finger.name, vidPid(m_finger.vendor, m_finger.product));
    if (!m_fingerError.isEmpty())
        m_fingerDetail += QLatin1Char('\n') + m_fingerError;
}

void ClinicModel::fillPenCard()
{
    m_penSource = m_backend->inputSource(m_pen);
    if (!m_pen.ok) {
        if (m_penValue.isEmpty())
            m_penValue = QStringLiteral("no stylus");
        return;
    }
    m_penValue = QStringLiteral("R=%1 (%2)").arg(m_pen.r).arg(orientationName(m_pen.r));
    m_penDetail = QStringLiteral("%1\n%2").arg(m_pen.name, vidPid(m_pen.vendor, m_pen.product));
    if (!m_penError.isEmpty())
        m_penDetail += QLatin1Char('\n') + m_penError;
}

void ClinicModel::probeTilt()
{
    m_tilt = TiltBack::TiltProbe::probe();
    fillTiltCard();
}

void ClinicModel::bindTiltSignals()
{
    QDBusConnection::systemBus().connect(
        QStringLiteral("net.hadess.SensorProxy"),
        QStringLiteral("/net/hadess/SensorProxy"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onSensorProxyChanged()));
}

void ClinicModel::onSensorProxyChanged()
{
    probeOutputTransform();
    probeTilt();
    buildReport();
    emit changed();
}

void ClinicModel::fillTiltCard()
{
    m_tiltSource = QStringLiteral("sysfs + udev + SensorProxy");
    m_tiltBackend = m_tiltSource;
    m_tiltCanApply = m_tilt.honesty == TiltBack::TiltHonesty::Readable
        && !TiltBack::kernelMatrixBlocksApply(m_tilt.kernelMatrix)
        && (!m_tilt.name.isEmpty() || !m_tilt.modalias.isEmpty());
    m_tiltCanSolve = false;
    m_tiltValue = m_tilt.honestyText;
    if (m_tilt.honesty == TiltBack::TiltHonesty::NoSensor) {
        m_tiltDetail = m_tilt.proxyPresent
            ? QStringLiteral("No IIO accelerometer. SensorProxy HasAccelerometer=%1.")
                  .arg(m_tilt.hasAccelerometer ? QStringLiteral("true")
                                               : QStringLiteral("false"))
            : QStringLiteral("No IIO accelerometer. SensorProxy is not on the bus.");
        return;
    }
    QStringList d;
    if (!m_tilt.name.isEmpty())
        d << m_tilt.name;
    if (!m_tilt.modalias.isEmpty())
        d << m_tilt.modalias;
    if (!m_tilt.label.isEmpty())
        d << QStringLiteral("label %1").arg(m_tilt.label);
    if (!m_tilt.extraId.isEmpty() && m_tilt.extraId != m_tilt.name)
        d << m_tilt.extraId;
    d << QStringLiteral("kernel matrix %1")
             .arg(m_tilt.kernelMatrix.isEmpty() ? QStringLiteral("(none)")
                                                : m_tilt.kernelMatrix);
    d << QStringLiteral("udev ACCEL_MOUNT_MATRIX %1")
             .arg(m_tilt.udevMatrix.isEmpty() ? QStringLiteral("(none)")
                                              : m_tilt.udevMatrix);
    {
        const int idx = TiltBack::mountMatrixIndex(
            !m_tiltLastApplied.isEmpty() ? m_tiltLastApplied : m_tilt.udevMatrix);
        if (idx >= 0)
            d << QStringLiteral("%1/8  %2")
                     .arg(idx)
                     .arg(TiltBack::mountMatrixAt(idx));
    }
    if (!m_tilt.proxyType.isEmpty())
        d << QStringLiteral("proxy %1").arg(m_tilt.proxyType);
    const QString e = m_tilt.orientation.isEmpty() ? QStringLiteral("(none)")
                                                   : m_tilt.orientation;
    d << QStringLiteral("enum=%1  T=%2").arg(e, m_outputTransform);
    {
        const QStringList order = {QStringLiteral("bottom"), QStringLiteral("right"),
                                   QStringLiteral("top"), QStringLiteral("left")};
        QStringList got;
        for (const QString &edge : order) {
            if (m_tiltHolds.contains(edge))
                got << edge;
        }
        if (!got.isEmpty())
            d << QStringLiteral("holds %1").arg(got.join(QLatin1Char(' ')));
        m_tiltCanSolve = TiltBack::tiltHoldCount(m_tiltHolds) >= 2;
        if (m_tiltCanSolve) {
            QString solved;
            double residual = 0;
            QString serr;
            const int idx = TiltBack::solveMountMatrix(m_tiltHolds, &solved,
                                                       &residual, &serr,
                                                       m_tilt.udevMatrix);
            if (idx >= 0)
                d << QStringLiteral("solve %1/8  %2").arg(idx).arg(solved);
            else if (!serr.isEmpty())
                d << serr;
        }
    }
    if (!m_tilt.reason.isEmpty() && m_tilt.honesty != TiltBack::TiltHonesty::Readable)
        d << m_tilt.reason;
    if (TiltBack::kernelMatrixBlocksApply(m_tilt.kernelMatrix))
        d << QStringLiteral("apply refused: kernel matrix is not identity");
    if (m_tilt.honesty == TiltBack::TiltHonesty::Readable
        && !TiltBack::accelHelperInstalled())
        d << TiltBack::accelHelperHint();
    if (!m_tiltError.isEmpty())
        d << m_tiltError;
    m_tiltDetail = d.join(QLatin1Char('\n'));
}

void ClinicModel::buildReport()
{
    QStringList lines;
    lines << QStringLiteral("TiltBack clinic report")
          << QStringLiteral("Backend: %1").arg(m_backend->id())
          << QStringLiteral("DMI: %1 / %2 / %3").arg(m_dmiVendor, m_dmiProduct, m_dmiBoard)
          << QStringLiteral("DRM panel-orientation: %1").arg(m_panelOrientation)
          << QStringLiteral("Output: %1").arg(m_outputName.isEmpty() ? QStringLiteral("(none)") : m_outputName)
          << QStringLiteral("T live: %1").arg(m_outputTransform);
    if (!m_persistedTransform.isEmpty())
        lines << QStringLiteral("T persisted: %1").arg(m_persistedTransform);
    lines << QStringLiteral("Tilt: %1").arg(m_tilt.honestyText);
    if (!m_tilt.name.isEmpty())
        lines << QStringLiteral("IIO: %1").arg(m_tilt.name);
    if (!m_tilt.modalias.isEmpty())
        lines << QStringLiteral("modalias: %1").arg(m_tilt.modalias);
    if (!m_tilt.extraId.isEmpty() && m_tilt.extraId != m_tilt.name)
        lines << QStringLiteral("ACPI/OF: %1").arg(m_tilt.extraId);
    if (!m_tilt.sysPath.isEmpty())
        lines << QStringLiteral("IIO sys: %1").arg(m_tilt.sysPath);
    lines << QStringLiteral("kernel matrix: %1")
                 .arg(m_tilt.kernelMatrix.isEmpty() ? QStringLiteral("(none)")
                                                    : m_tilt.kernelMatrix);
    lines << QStringLiteral("udev ACCEL_MOUNT_MATRIX: %1")
                 .arg(m_tilt.udevMatrix.isEmpty() ? QStringLiteral("(none)")
                                                  : m_tilt.udevMatrix);
    lines << QStringLiteral("IIO_SENSOR_PROXY_TYPE: %1")
                 .arg(m_tilt.proxyType.isEmpty() ? QStringLiteral("(none)")
                                                : m_tilt.proxyType);
    lines << QStringLiteral("HasAccelerometer: %1")
                 .arg(!m_tilt.proxyPresent
                          ? QStringLiteral("n/a")
                          : (m_tilt.hasAccelerometer ? QStringLiteral("true")
                                                     : QStringLiteral("false")));
    lines << QStringLiteral("enum: %1")
                 .arg(m_tilt.orientation.isEmpty() ? QStringLiteral("(none)")
                                                   : m_tilt.orientation);
    lines << QStringLiteral("Arrow: not inverted / not probed")
          << m_homeLine
          << m_persistLine
          << m_followStatus
          << QStringLiteral("Absolute devices:");
    if (m_reportDevices.isEmpty())
        lines << QStringLiteral("(none)");
    else
        lines << m_reportDevices;
    m_reportText = lines.join(QLatin1Char('\n'));
}

void ClinicModel::applyPicture(const QString &kscreen)
{
    const QString want = TiltBack::normalizeKscreen(kscreen);
    if (want.isEmpty()) {
        m_pictureError = QStringLiteral("unknown transform %1").arg(kscreen);
        emit changed();
        return;
    }
    if (!m_backend->canChangePicture()) {
        m_pictureError = QStringLiteral("backend cannot change");
        fillPictureCard();
        emit changed();
        return;
    }

    const TiltBack::OutputInfo live = m_backend->readOutput();
    if (live.ok && live.tKscreen == want) {
        m_pictureError = QStringLiteral("already %1").arg(want);
        m_outputName = live.name;
        m_liveKscreen = live.tKscreen;
        m_outputTransform = live.tKwin;
        fillPictureCard();
        emit changed();
        return;
    }

    const QString snapshot = !live.tKscreen.isEmpty()
        ? live.tKscreen
        : TiltBack::normalizeKscreen(m_outputTransform);

    if (!m_backend->setOutput(want, &m_pictureError)) {
        const QString err = m_pictureError;
        refresh();
        m_pictureError = err;
        fillPictureCard();
        emit changed();
        return;
    }

    const TiltBack::OutputInfo now = m_backend->readOutput();
    if (!now.ok || now.tKscreen != want) {
        const QString err = m_pictureError.isEmpty()
            ? QStringLiteral("backend did not change T to %1").arg(want)
            : m_pictureError;
        refresh();
        m_pictureError = err;
        fillPictureCard();
        emit changed();
        return;
    }

    m_outputName = now.name;
    m_liveKscreen = now.tKscreen;
    m_outputTransform = now.tKwin;
    m_revertKscreen = snapshot;
    m_picturePendingLabel = QStringLiteral("T=%1").arg(m_outputTransform);
    startPictureCountdown();
    refresh();
}

void ClinicModel::keepPicture()
{
    if (!m_picturePending)
        return;
    QString err;
    m_backend->commitOutput(&err);
    TiltBack::requestInstallGreeter();
    stopPictureCountdown();
    refresh();
    if (!err.isEmpty()) {
        m_pictureError = err;
        fillPictureCard();
        emit changed();
    }
}

void ClinicModel::revertPicture()
{
    if (!m_picturePending)
        return;
    const QString target = m_revertKscreen;
    stopPictureCountdown();
    if (target.isEmpty()) {
        refresh();
        return;
    }
    m_backend->setOutput(target, &m_pictureError);
    refresh();
}

bool ClinicModel::anyPending() const
{
    return m_picturePending || m_fingerPending || m_penPending || m_tiltPending;
}

void ClinicModel::ensureTimer()
{
    if (!m_revertTimer->isActive())
        m_revertTimer->start();
}

void ClinicModel::startPictureCountdown()
{
    m_picturePending = true;
    m_pictureSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopPictureCountdown()
{
    m_picturePending = false;
    m_pictureSeconds = 0;
    m_picturePendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

void ClinicModel::startFingerCountdown()
{
    m_fingerPending = true;
    m_fingerSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopFingerCountdown()
{
    m_fingerPending = false;
    m_fingerSeconds = 0;
    m_fingerPendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

void ClinicModel::startPenCountdown()
{
    m_penPending = true;
    m_penSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopPenCountdown()
{
    m_penPending = false;
    m_penSeconds = 0;
    m_penPendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

void ClinicModel::startTiltCountdown()
{
    m_tiltPending = true;
    m_tiltSeconds = 10;
    ensureTimer();
    syncClinicHold();
}

void ClinicModel::stopTiltCountdown()
{
    m_tiltPending = false;
    m_tiltSeconds = 0;
    m_tiltPendingLabel.clear();
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
}

bool ClinicModel::requestTiltOp(bool remove, const QString &matrix, QString *error)
{
    QString nonce;
    const bool ok = remove
        ? TiltBack::requestAccelRemove(&nonce, error)
        : TiltBack::requestAccelApply(m_tilt.name, m_tilt.modalias, matrix, &nonce, error);
    if (!ok)
        return false;
    if (TiltBack::waitAccelStamp(nonce, error))
        return true;
    if (!TiltBack::accelHelperInstalled()) {
        if (error && error->isEmpty())
            *error = TiltBack::accelHelperHint();
        return false;
    }
    return false;
}

void ClinicModel::applyTilt(const QString &kind)
{
    m_tilt = TiltBack::TiltProbe::probe();
    const QString current = !m_tiltLastApplied.isEmpty() ? m_tiltLastApplied
                                                         : m_tilt.udevMatrix;
    const QString matrix = TiltBack::normalizeMountMatrix(kind, current);
    if (m_tilt.honesty != TiltBack::TiltHonesty::Readable) {
        m_tiltError = QStringLiteral("Tilt apply needs a readable IMU");
        fillTiltCard();
        emit changed();
        return;
    }
    if (TiltBack::kernelMatrixBlocksApply(m_tilt.kernelMatrix)) {
        m_tiltError = QStringLiteral("kernel matrix is not identity; refuse compose");
        fillTiltCard();
        emit changed();
        return;
    }
    if (m_tilt.name.isEmpty() && m_tilt.modalias.isEmpty()) {
        m_tiltError = QStringLiteral("no IIO name or modalias");
        fillTiltCard();
        emit changed();
        return;
    }
    if (matrix.isEmpty()) {
        m_tiltError = QStringLiteral("unknown tilt matrix");
        fillTiltCard();
        emit changed();
        return;
    }
    if (!m_tiltPending) {
        const bool haveRule = QFile::exists(
            QStringLiteral("/etc/udev/rules.d/61-tiltback-accel.rules"));
        m_tiltRevertRemove = !haveRule;
        m_tiltRevertMatrix = haveRule ? TiltBack::canonicalMountMatrix(current)
                                      : QString();
    }
    QString err;
    if (!requestTiltOp(false, matrix, &err)) {
        m_tiltError = err;
        fillTiltCard();
        emit changed();
        return;
    }
    m_tiltError.clear();
    m_tiltLastApplied = matrix;
    m_tiltPendingLabel = QStringLiteral("sensor reload  %1").arg(matrix);
    startTiltCountdown();
    probeOutputTransform();
    probeTilt();
    buildReport();
    emit changed();
}

void ClinicModel::keepTilt()
{
    if (!m_tiltPending)
        return;
    m_tiltKeptMatrix = m_tiltLastApplied;
    stopTiltCountdown();
    refresh();
}

void ClinicModel::revertTilt()
{
    const bool haveRule = QFile::exists(
        QStringLiteral("/etc/udev/rules.d/61-tiltback-accel.rules"));
    if (!m_tiltPending && !haveRule && m_tiltRevertMatrix.isEmpty()) {
        refresh();
        return;
    }
    stopTiltCountdown();
    QString err;
    const bool ok = m_tiltRevertRemove
        ? requestTiltOp(true, {}, &err)
        : requestTiltOp(false, m_tiltRevertMatrix, &err);
    if (!ok)
        m_tiltError = err;
    m_tiltLastApplied.clear();
    refresh();
}

void ClinicModel::captureTilt(const QString &edge)
{
    const QString e = TiltBack::normalizeTiltEdge(edge);
    m_tilt = TiltBack::TiltProbe::probe();
    if (e.isEmpty()) {
        m_tiltError = QStringLiteral("hold is bottom|right|top|left");
        fillTiltCard();
        emit changed();
        return;
    }
    if (m_tilt.honesty != TiltBack::TiltHonesty::Readable || !m_tilt.rawOk) {
        m_tiltError = QStringLiteral("Tilt capture needs readable sysfs raw");
        fillTiltCard();
        emit changed();
        return;
    }
    double x, y, z;
    if (!TiltBack::parseAccelVec(m_tilt.raw, &x, &y, &z)) {
        m_tiltError = QStringLiteral("cannot parse raw %1").arg(m_tilt.raw);
        fillTiltCard();
        emit changed();
        return;
    }
    QString err;
    if (!TiltBack::accelVecUsable(x, y, z, &err)) {
        m_tiltError = err;
        fillTiltCard();
        emit changed();
        return;
    }
    if (!TiltBack::saveTiltHold(e, m_tilt.raw, &err)) {
        m_tiltError = err;
        fillTiltCard();
        emit changed();
        return;
    }
    TiltBack::loadTiltHolds(&m_tiltHolds, nullptr);
    m_tiltError.clear();
    fillTiltCard();
    buildReport();
    emit changed();
}

void ClinicModel::solveTilt()
{
    m_tilt = TiltBack::TiltProbe::probe();
    TiltBack::loadTiltHolds(&m_tiltHolds, nullptr);
    QString matrix;
    double residual = 0;
    QString err;
    const int idx = TiltBack::solveMountMatrix(m_tiltHolds, &matrix, &residual, &err,
                                              m_tilt.udevMatrix);
    if (idx < 0) {
        m_tiltError = err;
        fillTiltCard();
        emit changed();
        return;
    }
    applyTilt(matrix);
}

void ClinicModel::onRevertTick()
{
    if (m_picturePending) {
        m_pictureSeconds--;
        if (m_pictureSeconds <= 0)
            revertPicture();
    }
    if (m_fingerPending) {
        m_fingerSeconds--;
        if (m_fingerSeconds <= 0)
            revertFinger();
        else
            warnIfFollowFight(DigitizerClass::Finger);
    }
    if (m_penPending) {
        m_penSeconds--;
        if (m_penSeconds <= 0)
            revertPen();
        else
            warnIfFollowFight(DigitizerClass::Pen);
    }
    if (m_tiltPending) {
        m_tiltSeconds--;
        if (m_tiltSeconds <= 0)
            revertTilt();
    }
    if (!anyPending())
        m_revertTimer->stop();
    syncClinicHold();
    emit changed();
}

bool ClinicModel::resolveDigitizer(DigitizerClass kind, Digitizer *out)
{
    if (!out)
        return false;
    const Digitizer *identity = nullptr;
    if (kind == DigitizerClass::Finger && m_finger.ok)
        identity = &m_finger;
    else if (kind == DigitizerClass::Pen && m_pen.ok)
        identity = &m_pen;
    *out = m_backend->resolve(kind, identity);
    return out->ok;
}

void ClinicModel::applyDigitizer(DigitizerClass kind, int r)
{
    const bool finger = kind == DigitizerClass::Finger;
    QString &err = finger ? m_fingerError : m_penError;
    Digitizer &slot = finger ? m_finger : m_pen;

    if (!m_backend->canChangeInput()) {
        err = QStringLiteral("backend cannot change");
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    Digitizer live;
    if (!resolveDigitizer(kind, &live)) {
        err = QStringLiteral("no named digitizer");
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }
    slot = live;

    if (live.r == r) {
        err = QStringLiteral("already R=%1").arg(r);
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    const int snapshot = live.r;
    if (!m_backend->setResidual(live, r, &err)) {
        const QString keep = err;
        refresh();
        err = keep;
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    bool ok = false;
    const int now = m_backend->getResidual(live, &ok);
    const bool sameInvert = (r == 4 || r == 8) && (now == 4 || now == 8);
    if (!ok || (now != r && !sameInvert)) {
        err = QStringLiteral("backend did not change R to %1").arg(r);
        refresh();
        if (finger)
            m_fingerError = QStringLiteral("backend did not change R to %1").arg(r);
        else
            m_penError = QStringLiteral("backend did not change R to %1").arg(r);
        if (finger)
            fillFingerCard();
        else
            fillPenCard();
        emit changed();
        return;
    }

    live.r = now;
    slot = live;
    if (finger) {
        m_fingerRevertR = snapshot;
        m_fingerAppliedR = r;
        m_fingerPendingLabel = QStringLiteral("R=%1 (%2)").arg(r).arg(orientationName(r));
        startFingerCountdown();
    } else {
        m_penRevertR = snapshot;
        m_penAppliedR = r;
        m_penPendingLabel = QStringLiteral("R=%1 (%2)").arg(r).arg(orientationName(r));
        startPenCountdown();
    }
    refresh();
    warnIfFollowFight(kind);
    if (kind == DigitizerClass::Finger)
        fillFingerCard();
    else
        fillPenCard();
    emit changed();
}

void ClinicModel::warnIfFollowFight(DigitizerClass kind)
{
    Digitizer live;
    if (!resolveDigitizer(kind, &live))
        return;
    const int applied = (kind == DigitizerClass::Finger) ? m_fingerAppliedR : m_penAppliedR;
    const bool pending = (kind == DigitizerClass::Finger) ? m_fingerPending : m_penPending;
    if (!pending || applied != 0 || live.r != 8)
        return;
    const QString warn = QStringLiteral("R jumped to 8 (is follow running?)");
    if (kind == DigitizerClass::Finger) {
        m_finger = live;
        m_fingerError = warn;
        fillFingerCard();
    } else {
        m_pen = live;
        m_penError = warn;
        fillPenCard();
    }
}

void ClinicModel::applyFinger(int r)
{
    applyDigitizer(DigitizerClass::Finger, r);
}

void ClinicModel::applyPen(int r)
{
    applyDigitizer(DigitizerClass::Pen, r);
}

void ClinicModel::keepFinger()
{
    if (!m_fingerPending)
        return;
    stopFingerCountdown();
    m_fingerError = QStringLiteral("Kept — follow restamps home R");
    fillFingerCard();
    emit changed();
}

void ClinicModel::keepPen()
{
    if (!m_penPending)
        return;
    stopPenCountdown();
    m_penError = QStringLiteral("Kept — follow restamps home R");
    fillPenCard();
    emit changed();
}

void ClinicModel::revertFinger()
{
    if (!m_fingerPending)
        return;
    const int target = m_fingerRevertR;
    stopFingerCountdown();
    Digitizer live;
    if (resolveDigitizer(DigitizerClass::Finger, &live))
        m_backend->setResidual(live, target, &m_fingerError);
    refresh();
}

void ClinicModel::revertPen()
{
    if (!m_penPending)
        return;
    const int target = m_penRevertR;
    stopPenCountdown();
    Digitizer live;
    if (resolveDigitizer(DigitizerClass::Pen, &live))
        m_backend->setResidual(live, target, &m_penError);
    refresh();
}

void ClinicModel::saveHome()
{
    const TiltBack::OutputInfo live = m_backend->readOutput();
    if (live.ok) {
        m_outputName = live.name;
        m_liveKscreen = live.tKscreen;
        m_outputTransform = live.tKwin;
    }
    probeInputs();
    probeTilt();
    TiltBack::HomeProfile home;
    home.tHome = m_outputTransform;
    home.rTouch = m_finger.ok ? m_finger.r : 0;
    home.rPen = m_pen.ok ? m_pen.r : 0;
    home.fingerName = m_finger.name;
    home.fingerVendor = m_finger.vendor;
    home.fingerProduct = m_finger.product;
    home.penName = m_pen.name;
    home.penVendor = m_pen.vendor;
    home.penProduct = m_pen.product;
    home.accelMountMatrix = !m_tilt.udevMatrix.isEmpty()
        ? TiltBack::canonicalMountMatrix(m_tilt.udevMatrix)
        : m_tiltKeptMatrix;
    m_home = home;

    QString homeErr;
    const bool homeOk = TiltBack::saveHomeFile(home, &homeErr);
    m_homeLine = QStringLiteral("Home  T=%1  R_touch=%2  R_pen=%3")
                     .arg(home.tHome)
                     .arg(home.rTouch)
                     .arg(home.rPen);
    if (!home.accelMountMatrix.isEmpty())
        m_homeLine += QStringLiteral("  tilt=%1").arg(home.accelMountMatrix);
    m_persistHow = m_backend->persistHow();
    if (m_backend->persistKcminput()) {
        QString persistErr;
        const bool persistOk = TiltBack::persistKcminputrc(home, &persistErr);
        if (persistOk)
            m_persistLine = QStringLiteral("persist ok (kcminputrc Orientation=)");
        else
            m_persistLine = QStringLiteral("persist failed: %1").arg(persistErr);
    } else {
        m_persistLine = QStringLiteral("persist ok (home.json only)");
    }
    if (!homeOk)
        m_persistLine += QStringLiteral(" — home.json: %1").arg(homeErr);
    else if (TiltBack::homeProfilePath().startsWith(QLatin1String("/tmp")))
        m_persistLine += QStringLiteral(" — home.json in /tmp");
    if (homeOk)
        TiltBack::requestInstallGreeter();
    emit changed();
}

void ClinicModel::installFollow()
{
    QString err;
    if (TiltBack::installFollow(QCoreApplication::applicationFilePath(), &err) != 0)
        m_followStatus = QStringLiteral("Follow  install failed: %1").arg(err);
    else
        refreshFollowStatus();
    emit changed();
}

void ClinicModel::syncClinicHold()
{
    if (anyPending())
        TiltBack::writeClinicHold();
    else
        TiltBack::clearClinicHold();
}

void ClinicModel::loadHomeState()
{
    const bool haveFile = QFile::exists(TiltBack::homeProfilePath());
    m_home = TiltBack::loadHome(m_dmiProduct, m_backend->id() != QLatin1String("gnome"));
    m_persistHow = m_backend->persistHow();
    if (m_home.tHome.isEmpty()) {
        m_homeLine = QStringLiteral("Home  (none — Save home first)");
        if (m_persistLine.isEmpty())
            m_persistLine = QStringLiteral("persist not written this session");
        return;
    }
    m_homeLine = QStringLiteral("Home  T=%1  R_touch=%2  R_pen=%3")
                     .arg(m_home.tHome)
                     .arg(m_home.rTouch)
                     .arg(m_home.rPen);
    if (!m_home.accelMountMatrix.isEmpty())
        m_homeLine += QStringLiteral("  tilt=%1").arg(m_home.accelMountMatrix);
    if (m_persistLine.isEmpty()) {
        m_persistLine = haveFile
            ? QStringLiteral("home.json loaded")
            : QStringLiteral("W620 DMI seed — Save home to persist");
    }
}

void ClinicModel::refreshFollowStatus()
{
    auto unitState = [](const QString &unit) {
        QProcess p;
        p.start(QStringLiteral("systemctl"),
                {QStringLiteral("--user"), QStringLiteral("is-active"), unit});
        if (!p.waitForFinished(3000))
            return QStringLiteral("unknown");
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    };
    const QString follow = unitState(QStringLiteral("tiltback-follow.service"));
    const QString w620 = unitState(QStringLiteral("tiltback-w620.service"));
    if (w620 == QLatin1String("active"))
        m_followStatus = QStringLiteral("Follow  tiltback-w620.service active (Python) — Install to replace");
    else if (follow == QLatin1String("active"))
        m_followStatus = QStringLiteral("Follow  active");
    else
        m_followStatus = QStringLiteral("Follow  inactive — Install/start");
}
