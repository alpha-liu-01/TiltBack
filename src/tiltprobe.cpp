#include "tiltprobe.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QThread>
#include <QUuid>
#include <QVector>

#include <algorithm>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <sys/stat.h>

namespace TiltBack {
namespace {

QString readSysfs(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

bool readRawTriple(const QString &dir, QString *out, QString *err)
{
    const char *axes[] = {"in_accel_x_raw", "in_accel_y_raw", "in_accel_z_raw"};
    QString parts[3];
    for (int i = 0; i < 3; ++i) {
        QFile f(dir + QLatin1Char('/') + QLatin1String(axes[i]));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *err = QStringLiteral("%1 (%2)")
                       .arg(QString::fromLocal8Bit(strerror(errno)),
                            QLatin1String(axes[i]));
            return false;
        }
        parts[i] = QString::fromUtf8(f.readAll()).trimmed();
    }
    *out = QStringLiteral("%1,%2,%3").arg(parts[0], parts[1], parts[2]);
    return true;
}

QString walkModalias(QString path)
{
    QDir d(path);
    for (int i = 0; i < 14; ++i) {
        const QString m = readSysfs(d.filePath(QStringLiteral("modalias")));
        if (!m.isEmpty())
            return m;
        const QString uevent = readSysfs(d.filePath(QStringLiteral("uevent")));
        for (const QString &line : uevent.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("MODALIAS=")))
                return line.mid(9);
        }
        if (!d.cdUp())
            break;
    }
    return {};
}

QMap<QString, QString> udevProperties(const QString &sysPath)
{
    QMap<QString, QString> out;
    QProcess p;
    p.start(QStringLiteral("udevadm"),
            {QStringLiteral("info"), QStringLiteral("-q"), QStringLiteral("property"),
             QStringLiteral("-p"), sysPath});
    if (!p.waitForFinished(2500) || p.exitCode() != 0)
        return out;
    const QString text = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        out.insert(line.left(eq), line.mid(eq + 1));
    }
    return out;
}

QString unixMode(const QString &devNode)
{
    if (devNode.isEmpty())
        return {};
    struct stat st {};
    if (stat(QFile::encodeName(devNode).constData(), &st) != 0)
        return {};
    return QStringLiteral("%1:%2 %03o")
        .arg(static_cast<qlonglong>(st.st_uid))
        .arg(static_cast<qlonglong>(st.st_gid))
        .arg(static_cast<unsigned>(st.st_mode & 0777));
}

bool knownEnum(const QString &e)
{
    return e == QLatin1String("normal") || e == QLatin1String("bottom-up")
        || e == QLatin1String("left-up") || e == QLatin1String("right-up");
}

int rankCandidate(const QString &name, const QString &label, bool rawOk)
{
    if (label == QLatin1String("accel-display"))
        return 0;
    const QString n = name.toLower();
    if (n.contains(QLatin1String("accel")) && !n.contains(QLatin1String("gyro")))
        return 1;
    if (rawOk)
        return 2;
    return 3;
}

struct Candidate {
    QString path;
    QString name;
    QString label;
    bool rawOk = false;
    QString raw;
    QString rawError;
    int rank = 3;
};

void classify(TiltFact *f)
{
    const bool haveIio = !f->name.isEmpty();
    const bool proxyAccel = f->proxyPresent && f->hasAccelerometer;
    if (!haveIio && !proxyAccel) {
        f->honesty = TiltHonesty::NoSensor;
        f->honestyText = QStringLiteral("no sensor");
        return;
    }
    if (haveIio && !f->rawOk) {
        f->honesty = TiltHonesty::Unreadable;
        f->reason = f->rawError.isEmpty() ? QStringLiteral("raw denied") : f->rawError;
        if (!f->devMode.isEmpty())
            f->reason += QStringLiteral("; %1 %2").arg(f->devNode, f->devMode);
        f->honestyText = QStringLiteral("unreadable (%1)").arg(f->reason);
        return;
    }
    if (!f->proxyPresent) {
        f->honesty = TiltHonesty::Unreadable;
        f->reason = QStringLiteral("no SensorProxy");
        f->honestyText = QStringLiteral("unreadable (no SensorProxy)");
        return;
    }
    if (!f->hasAccelerometer) {
        f->honesty = TiltHonesty::Unreadable;
        f->reason = QStringLiteral("HasAccelerometer=false");
        f->honestyText = QStringLiteral("unreadable (HasAccelerometer=false)");
        return;
    }
    if (!knownEnum(f->orientation)) {
        f->honesty = TiltHonesty::Unreadable;
        const QString e = f->orientation.isEmpty() ? QStringLiteral("missing")
                                                   : f->orientation;
        f->reason = QStringLiteral("undefined");
        if (!f->devMode.isEmpty() && f->devMode.contains(QLatin1String("0600")))
            f->reason += QStringLiteral("; %1 %2").arg(f->devNode, f->devMode);
        f->honestyText = QStringLiteral("unreadable (%1)").arg(e);
        if (f->honestyText == QStringLiteral("unreadable (undefined)")
            && !f->devMode.isEmpty()) {
            f->honestyText = QStringLiteral("unreadable (undefined; %1 %2)")
                                 .arg(f->devNode, f->devMode);
        }
        return;
    }
    f->honesty = TiltHonesty::Readable;
    f->honestyText = QStringLiteral("readable");
}

} // namespace

TiltFact TiltProbe::probe()
{
    TiltFact f;
    QVector<Candidate> found;
    const QDir root(QStringLiteral("/sys/bus/iio/devices"));
    const QStringList nodes = root.entryList(QStringList() << QStringLiteral("iio:device*"),
                                             QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &node : nodes) {
        const QString path = root.absoluteFilePath(node);
        if (!QFile::exists(path + QStringLiteral("/in_accel_x_raw")))
            continue;
        Candidate c;
        c.path = path;
        c.name = readSysfs(path + QStringLiteral("/name"));
        c.label = readSysfs(path + QStringLiteral("/label"));
        c.rawOk = readRawTriple(path, &c.raw, &c.rawError);
        c.rank = rankCandidate(c.name, c.label, c.rawOk);
        found.append(c);
    }

    std::sort(found.begin(), found.end(),
              [](const Candidate &a, const Candidate &b) { return a.rank < b.rank; });

    if (!found.isEmpty()) {
        const Candidate &c = found.first();
        f.sysPath = c.path;
        f.name = c.name;
        f.label = c.label;
        f.rawOk = c.rawOk;
        f.raw = c.raw;
        f.rawError = c.rawError;
        f.kernelMatrix = readSysfs(c.path + QStringLiteral("/in_accel_mount_matrix"));
        f.modalias = walkModalias(c.path + QStringLiteral("/device"));
        if (f.modalias.isEmpty() && !c.name.isEmpty()
            && !c.name.contains(QLatin1Char(':'))) {
            f.modalias = QStringLiteral("platform:%1").arg(c.name);
        }
        const QString ofCompat = readSysfs(c.path + QStringLiteral("/of_node/compatible"));
        if (!ofCompat.isEmpty())
            f.extraId = ofCompat.split(QChar(QChar::Null)).value(0);
        else if (c.name.startsWith(QLatin1String("i2c-")) && c.name.contains(QLatin1Char(':'))) {
            const QString rest = c.name.mid(4);
            f.extraId = rest.left(rest.indexOf(QLatin1Char(':')));
        }

        const auto props = udevProperties(c.path);
        f.udevMatrix = props.value(QStringLiteral("ACCEL_MOUNT_MATRIX"));
        f.proxyType = props.value(QStringLiteral("IIO_SENSOR_PROXY_TYPE"));
        f.devNode = props.value(QStringLiteral("DEVNAME"));
        if (f.devNode.isEmpty()) {
            const QFileInfo base(c.path);
            const QString guess = QStringLiteral("/dev/%1").arg(base.fileName());
            if (QFileInfo::exists(guess))
                f.devNode = guess;
        }
        f.devMode = unixMode(f.devNode);
    }

    QDBusConnection sys = QDBusConnection::systemBus();
    QDBusInterface iface(QStringLiteral("net.hadess.SensorProxy"),
                         QStringLiteral("/net/hadess/SensorProxy"),
                         QStringLiteral("net.hadess.SensorProxy"), sys);
    f.proxyPresent = iface.isValid();
    if (f.proxyPresent) {
        iface.call(QStringLiteral("ClaimAccelerometer"));
        const QVariant has = iface.property("HasAccelerometer");
        f.hasAccelerometer = has.isValid() && has.toBool();
        f.orientation = iface.property("AccelerometerOrientation").toString();
    }

    classify(&f);
    return f;
}

namespace {

const QString kMountMatrix[] = {
    QStringLiteral("1, 0, 0; 0, 1, 0; 0, 0, 1"),
    QStringLiteral("0, 1, 0; -1, 0, 0; 0, 0, 1"),
    QStringLiteral("-1, 0, 0; 0, -1, 0; 0, 0, 1"),
    QStringLiteral("0, -1, 0; 1, 0, 0; 0, 0, 1"),
    QStringLiteral("1, 0, 0; 0, 1, 0; 0, 0, -1"),
    QStringLiteral("0, 1, 0; -1, 0, 0; 0, 0, -1"),
    QStringLiteral("-1, 0, 0; 0, -1, 0; 0, 0, -1"),
    QStringLiteral("0, -1, 0; 1, 0, 0; 0, 0, -1"),
};

QString compactMatrix(const QString &matrix)
{
    QString n = matrix.trimmed();
    n.remove(QLatin1Char(' '));
    return n;
}

} // namespace

QString identityMountMatrix()
{
    return kMountMatrix[0];
}

QString wikiMountMatrix()
{
    return kMountMatrix[5];
}

int mountMatrixCount()
{
    return 8;
}

QString mountMatrixAt(int index)
{
    if (index < 0 || index >= mountMatrixCount())
        return {};
    return kMountMatrix[index];
}

int mountMatrixIndex(const QString &matrix)
{
    const QString n = compactMatrix(matrix);
    if (n.isEmpty())
        return -1;
    for (int i = 0; i < mountMatrixCount(); ++i) {
        if (compactMatrix(kMountMatrix[i]) == n)
            return i;
    }
    return -1;
}

QString nextMountMatrix(const QString &current)
{
    const int i = mountMatrixIndex(current);
    return kMountMatrix[i < 0 ? 0 : (i + 1) % mountMatrixCount()];
}

QString prevMountMatrix(const QString &current)
{
    const int i = mountMatrixIndex(current);
    const int n = mountMatrixCount();
    return kMountMatrix[i < 0 ? n - 1 : (i + n - 1) % n];
}

QString canonicalMountMatrix(const QString &matrix)
{
    const int i = mountMatrixIndex(matrix);
    if (i >= 0)
        return kMountMatrix[i];
    return matrix.trimmed();
}

QString normalizeMountMatrix(const QString &kind, const QString &current)
{
    const QString k = kind.trimmed().toLower();
    if (k == QLatin1String("identity") || k == QLatin1String("id"))
        return identityMountMatrix();
    if (k == QLatin1String("wiki") || k == QLatin1String("trogdor"))
        return wikiMountMatrix();
    if (k == QLatin1String("next"))
        return nextMountMatrix(current);
    if (k == QLatin1String("prev") || k == QLatin1String("previous"))
        return prevMountMatrix(current);
    bool ok = false;
    const int idx = k.toInt(&ok);
    if (ok && idx >= 0 && idx < mountMatrixCount())
        return kMountMatrix[idx];
    return canonicalMountMatrix(kind);
}

bool kernelMatrixBlocksApply(const QString &kernelMatrix)
{
    const QString raw = kernelMatrix.trimmed();
    if (raw.isEmpty())
        return false;
    QString n = raw;
    n.remove(QLatin1Char(' '));
    return n != QLatin1String("1,0,0;0,1,0;0,0,1");
}

bool accelHelperInstalled()
{
    return QFile::exists(QStringLiteral("/usr/lib/systemd/system/tiltback-accel.path"))
        || QFile::exists(QStringLiteral("/lib/systemd/system/tiltback-accel.path"))
        || QFile::exists(QStringLiteral("/etc/systemd/system/tiltback-accel.path"));
}

QString accelHelperHint()
{
    return QStringLiteral(
        "sudo install -D -m 0755 /usr/libexec/tiltback/apply-accel.sh "
        "/usr/libexec/tiltback/apply-accel.sh\n"
        "sudo cp /usr/lib/systemd/system/tiltback-accel.service "
        "/usr/lib/systemd/system/tiltback-accel.path /etc/systemd/system/\n"
        "sudo systemctl enable --now tiltback-accel.path\n"
        "Or one-shot after writing /run/tiltback/accel-request:\n"
        "sudo /usr/libexec/tiltback/apply-accel.sh");
}

QString newAccelNonce()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool writeAccelRequest(const QString &body, QString *error)
{
    QDir().mkpath(QStringLiteral("/run/tiltback"));
    const QString path = QStringLiteral("/run/tiltback/accel-request");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    f.write(body.toUtf8());
    f.close();
    return true;
}

bool requestAccelApply(const QString &name, const QString &modalias,
                       const QString &matrix, QString *nonce, QString *error)
{
    const QString n = newAccelNonce();
    if (nonce)
        *nonce = n;
    QString body = QStringLiteral("op=apply\nnonce=%1\nmatrix=%2\n").arg(n, matrix);
    if (!name.isEmpty())
        body += QStringLiteral("name=%1\n").arg(name);
    if (!modalias.isEmpty())
        body += QStringLiteral("modalias=%1\n").arg(modalias);
    return writeAccelRequest(body, error);
}

bool requestAccelRemove(QString *nonce, QString *error)
{
    const QString n = newAccelNonce();
    if (nonce)
        *nonce = n;
    return writeAccelRequest(QStringLiteral("op=remove\nnonce=%1\n").arg(n), error);
}

bool waitAccelStamp(const QString &nonce, QString *error)
{
    const QString path = QStringLiteral("/run/tiltback/last-accel-done");
    for (int i = 0; i < 40; ++i) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString text = QString::fromUtf8(f.readAll());
            if (text.contains(QStringLiteral("nonce=%1").arg(nonce))) {
                if (text.contains(QLatin1String("error="))) {
                    if (error) {
                        for (const QString &line : text.split(QLatin1Char('\n'))) {
                            if (line.startsWith(QLatin1String("error=")))
                                *error = line.mid(6);
                        }
                    }
                    return false;
                }
                return true;
            }
        }
        QThread::msleep(250);
    }
    if (error) {
        *error = accelHelperInstalled()
            ? QStringLiteral("accel helper timed out")
            : accelHelperHint();
    }
    return false;
}

QString normalizeTiltEdge(const QString &edge)
{
    const QString e = edge.trimmed().toLower();
    if (e == QLatin1String("bottom") || e == QLatin1String("b")
        || e == QLatin1String("down"))
        return QStringLiteral("bottom");
    if (e == QLatin1String("right") || e == QLatin1String("r"))
        return QStringLiteral("right");
    if (e == QLatin1String("top") || e == QLatin1String("t")
        || e == QLatin1String("up"))
        return QStringLiteral("top");
    if (e == QLatin1String("left") || e == QLatin1String("l"))
        return QStringLiteral("left");
    return {};
}

QString tiltHoldsPath()
{
    return QStringLiteral("/run/tiltback/tilt-holds");
}

bool parseAccelVec(const QString &csv, double *x, double *y, double *z)
{
    const QStringList parts = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return false;
    bool okx = false, oky = false, okz = false;
    const double vx = parts.at(0).trimmed().toDouble(&okx);
    const double vy = parts.at(1).trimmed().toDouble(&oky);
    const double vz = parts.at(2).trimmed().toDouble(&okz);
    if (!okx || !oky || !okz)
        return false;
    if (x)
        *x = vx;
    if (y)
        *y = vy;
    if (z)
        *z = vz;
    return true;
}

bool accelVecUsable(double x, double y, double z, QString *error)
{
    const double n = std::sqrt(x * x + y * y + z * z);
    if (n < 1e-6) {
        if (error)
            *error = QStringLiteral("accel reading is zero");
        return false;
    }
    if (std::fabs(z) / n > 0.85) {
        if (error)
            *error = QStringLiteral("flat / Z-dominant — hold an edge down");
        return false;
    }
    return true;
}

bool expectedEdgeDown(const QString &edge, double *x, double *y, double *z)
{
    // iio-sensor-proxy device frame: X right, Y native top, Z out.
    // Trogdor live (wiki on): +X → left-up. Four-hold 2026-09-22: wiki*raw
    // with +Y classified as bottom-up, −Y as normal. Buttons are "this
    // edge is down".
    const QString e = normalizeTiltEdge(edge);
    if (e == QLatin1String("bottom")) {
        *x = 0;
        *y = -1;
        *z = 0;
        return true;
    }
    if (e == QLatin1String("top")) {
        *x = 0;
        *y = 1;
        *z = 0;
        return true;
    }
    if (e == QLatin1String("right")) {
        *x = 1;
        *y = 0;
        *z = 0;
        return true;
    }
    if (e == QLatin1String("left")) {
        *x = -1;
        *y = 0;
        *z = 0;
        return true;
    }
    return false;
}

bool parseMount9(const QString &matrix, double m[9])
{
    QString t = matrix;
    t.replace(QLatin1Char(';'), QLatin1Char(','));
    const QStringList parts = t.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 9)
        return false;
    for (int i = 0; i < 9; ++i) {
        bool ok = false;
        m[i] = parts.at(i).trimmed().toDouble(&ok);
        if (!ok)
            return false;
    }
    return true;
}

void applyMount(const double m[9], double sx, double sy, double sz,
                double *dx, double *dy, double *dz)
{
    *dx = m[0] * sx + m[1] * sy + m[2] * sz;
    *dy = m[3] * sx + m[4] * sy + m[5] * sz;
    *dz = m[6] * sx + m[7] * sy + m[8] * sz;
}

bool normalize3(double *x, double *y, double *z)
{
    const double n = std::sqrt((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (n < 1e-9)
        return false;
    *x /= n;
    *y /= n;
    *z /= n;
    return true;
}

bool loadTiltHolds(QMap<QString, QString> *holds, QString *error)
{
    if (!holds)
        return false;
    holds->clear();
    QFile f(tiltHoldsPath());
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(tiltHoldsPath());
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString edge = normalizeTiltEdge(line.left(eq));
        const QString raw = line.mid(eq + 1).trimmed();
        double x, y, z;
        if (edge.isEmpty() || !parseAccelVec(raw, &x, &y, &z))
            continue;
        holds->insert(edge, QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(z));
    }
    return true;
}

bool saveTiltHold(const QString &edge, const QString &raw, QString *error)
{
    const QString e = normalizeTiltEdge(edge);
    double x, y, z;
    if (e.isEmpty() || !parseAccelVec(raw, &x, &y, &z)) {
        if (error)
            *error = QStringLiteral("bad hold");
        return false;
    }
    QMap<QString, QString> holds;
    if (!loadTiltHolds(&holds, error))
        return false;
    holds.insert(e, QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(z));
    QDir().mkpath(QStringLiteral("/run/tiltback"));
    QFile f(tiltHoldsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(tiltHoldsPath());
        return false;
    }
    for (auto it = holds.constBegin(); it != holds.constEnd(); ++it)
        f.write(QStringLiteral("%1=%2\n").arg(it.key(), it.value()).toUtf8());
    return true;
}

int tiltHoldCount(const QMap<QString, QString> &holds)
{
    int n = 0;
    for (auto it = holds.constBegin(); it != holds.constEnd(); ++it) {
        if (!normalizeTiltEdge(it.key()).isEmpty() && !it.value().isEmpty())
            ++n;
    }
    return n;
}

int solveMountMatrix(const QMap<QString, QString> &holds, QString *matrix,
                     double *residual, QString *error, const QString &prefer)
{
    struct Sample {
        double sx, sy, sz, dx, dy, dz;
    };
    QVector<Sample> samples;
    for (auto it = holds.constBegin(); it != holds.constEnd(); ++it) {
        const QString edge = normalizeTiltEdge(it.key());
        double sx, sy, sz, dx, dy, dz;
        if (edge.isEmpty() || !parseAccelVec(it.value(), &sx, &sy, &sz))
            continue;
        if (!accelVecUsable(sx, sy, sz, error))
            return -1;
        if (!expectedEdgeDown(edge, &dx, &dy, &dz))
            continue;
        if (!normalize3(&sx, &sy, &sz))
            continue;
        samples.append({sx, sy, sz, dx, dy, dz});
    }
    if (samples.size() < 2) {
        if (error)
            *error = QStringLiteral("need at least two edge-down holds");
        return -1;
    }
    bool diverse = false;
    for (int i = 0; i < samples.size() && !diverse; ++i) {
        for (int j = i + 1; j < samples.size(); ++j) {
            const double dot = samples[i].sx * samples[j].sx
                + samples[i].sy * samples[j].sy
                + samples[i].sz * samples[j].sz;
            if (std::fabs(dot) < 0.92)
                diverse = true;
        }
    }
    if (!diverse) {
        if (error)
            *error = QStringLiteral("holds are collinear — capture a different edge");
        return -1;
    }

    int best = -1;
    double bestScore = 1e9;
    double scores[8];
    for (int i = 0; i < mountMatrixCount(); ++i)
        scores[i] = 1e9;
    for (int i = 0; i < mountMatrixCount(); ++i) {
        double m[9];
        if (!parseMount9(mountMatrixAt(i), m))
            continue;
        double sum = 0;
        for (const Sample &s : samples) {
            double px, py, pz;
            applyMount(m, s.sx, s.sy, s.sz, &px, &py, &pz);
            // Score the panel plane only. Real holds lean; |Z| is not the edge.
            const double pn = std::sqrt(px * px + py * py);
            const double dn = std::sqrt(s.dx * s.dx + s.dy * s.dy);
            if (pn < 1e-9 || dn < 1e-9) {
                sum = 1e9;
                break;
            }
            const double ddx = px / pn - s.dx / dn;
            const double ddy = py / pn - s.dy / dn;
            sum += ddx * ddx + ddy * ddy;
        }
        const double mean = sum / samples.size();
        scores[i] = mean;
        if (mean < bestScore) {
            bestScore = mean;
            best = i;
        }
    }
    // In-plane holds cannot see Z sign (index 1 vs wiki 5). Prefer live udev.
    const int pref = mountMatrixIndex(prefer);
    if (pref >= 0 && std::fabs(scores[pref] - bestScore) < 1e-6)
        best = pref;
    if (residual)
        *residual = bestScore;
    if (best < 0 || bestScore > 0.5) {
        if (error)
            *error = QStringLiteral("solve missed the eight (residual %1) — use Next")
                         .arg(bestScore, 0, 'f', 3);
        return -1;
    }
    if (matrix)
        *matrix = mountMatrixAt(best);
    if (error)
        error->clear();
    return best;
}

bool selfTestTiltSolve(QString *error)
{
    double m[9];
    if (!parseMount9(wikiMountMatrix(), m)) {
        if (error)
            *error = QStringLiteral("cannot parse wiki matrix");
        return false;
    }
    // Orthogonal inverse is M^T.
    const double mt[9] = {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
    const char *edges[] = {"bottom", "right", "top", "left"};
    QMap<QString, QString> holds;
    for (const char *edge : edges) {
        double dx, dy, dz;
        if (!expectedEdgeDown(QLatin1String(edge), &dx, &dy, &dz)) {
            if (error)
                *error = QStringLiteral("bad edge %1").arg(QLatin1String(edge));
            return false;
        }
        double sx, sy, sz;
        applyMount(mt, dx, dy, dz, &sx, &sy, &sz);
        holds.insert(QLatin1String(edge),
                     QStringLiteral("%1,%2,%3").arg(sx).arg(sy).arg(sz));
    }
    QString matrix;
    double residual = 99;
    const int idx = solveMountMatrix(holds, &matrix, &residual, error,
                                    wikiMountMatrix());
    if (idx != 5 || mountMatrixIndex(matrix) != 5 || residual > 1e-6) {
        if (error)
            *error = QStringLiteral("expected wiki 5/8, got %1 residual %2")
                         .arg(idx)
                         .arg(residual);
        return false;
    }
    return true;
}

} // namespace TiltBack
