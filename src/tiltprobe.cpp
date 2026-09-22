#include "tiltprobe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QVector>

#include <algorithm>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>

#include <cerrno>
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
        const QVariant has = iface.property("HasAccelerometer");
        f.hasAccelerometer = has.isValid() && has.toBool();
        f.orientation = iface.property("AccelerometerOrientation").toString();
    }

    classify(&f);
    return f;
}

} // namespace TiltBack
