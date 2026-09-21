#include "kwininput.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>
#include <QtDBus/QDBusVariant>

namespace TiltBack {
namespace {

const char KWin[] = "org.kde.KWin";
const char InputIface[] = "org.kde.KWin.InputDevice";
const char MgrIface[] = "org.kde.KWin.InputDeviceManager";
const char PropsIface[] = "org.freedesktop.DBus.Properties";
const char Prefix[] = "/org/kde/KWin/InputDevice";

QString hexId(quint32 value)
{
    return QStringLiteral("%1").arg(value, 4, 16, QLatin1Char('0'));
}

QStringList sysNames()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};
    QDBusInterface mgr(QString::fromLatin1(KWin), QString::fromLatin1(Prefix),
                       QString::fromLatin1(PropsIface), bus);
    const QDBusReply<QDBusVariant> reply = mgr.call(
        QStringLiteral("Get"),
        QString::fromLatin1(MgrIface),
        QStringLiteral("devicesSysNames"));
    if (!reply.isValid())
        return {};
    const QVariant inner = reply.value().variant();
    if (inner.canConvert<QStringList>())
        return inner.toStringList();
    if (inner.canConvert<QDBusArgument>())
        return qdbus_cast<QStringList>(inner.value<QDBusArgument>());
    return {};
}

QVariantMap getAll(const QString &path)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(KWin), path, QString::fromLatin1(PropsIface),
        QStringLiteral("GetAll"));
    msg << QString::fromLatin1(InputIface);
    const QDBusMessage reply = bus.call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return {};
    const QVariant first = reply.arguments().at(0);
    if (first.canConvert<QVariantMap>())
        return first.toMap();
    if (first.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(first.value<QDBusArgument>());
    return {};
}

bool writeTextFile(const QString &path, const QByteArray &data, QString *error)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    if (f.write(data) != data.size()) {
        if (error)
            *error = QStringLiteral("short write %1").arg(path);
        return false;
    }
    return true;
}

QString libinputGroup(quint32 vendor, quint32 product, const QString &name)
{
    return QStringLiteral("[Libinput][%1][%2][%3]")
        .arg(vendor)
        .arg(product)
        .arg(name);
}

bool upsertOrientation(QString *text, const QString &group, int r)
{
    const QString line = QStringLiteral("Orientation=%1").arg(r);
    const int idx = text->indexOf(group);
    if (idx < 0) {
        if (!text->isEmpty() && !text->endsWith(QLatin1Char('\n')))
            *text += QLatin1Char('\n');
        *text += group + QLatin1Char('\n') + line + QLatin1Char('\n');
        return true;
    }
    const int sectionEnd = text->indexOf(QLatin1Char('\n'), idx);
    int pos = sectionEnd < 0 ? text->size() : sectionEnd + 1;
    int nextGroup = text->indexOf(QLatin1Char('['), pos);
    const int end = nextGroup < 0 ? text->size() : nextGroup;
    const QString section = text->mid(pos, end - pos);
    const int ori = section.indexOf(QLatin1String("Orientation="));
    if (ori >= 0) {
        const int abs = pos + ori;
        const int eol = text->indexOf(QLatin1Char('\n'), abs);
        text->replace(abs, (eol < 0 ? text->size() : eol) - abs, line);
    } else {
        text->insert(pos, line + QLatin1Char('\n'));
    }
    return true;
}

} // namespace

QVariant unwrap(const QVariant &value)
{
    if (value.canConvert<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

bool isDenied(const QString &name, bool touchpad, quint32 vendor, quint32 product)
{
    if (touchpad)
        return true;
    if (vendor == 0x04e8 && product == 0xa00a)
        return true;
    if (name.contains(QLatin1String("04e8:a00a")))
        return true;
    if (vendor == 0x2d1f && product == 0x000c && name.contains(QLatin1String("Mouse")))
        return true;
    if (name.contains(QLatin1String("WCOM0028:00 2D1F:000C Mouse")))
        return true;
    return false;
}

QString vidPid(quint32 vendor, quint32 product)
{
    return QStringLiteral("%1:%2").arg(hexId(vendor), hexId(product));
}

QVector<Digitizer> listKwinDigitizers()
{
    QVector<Digitizer> out;
    for (const QString &sys : sysNames()) {
        const QString path = QStringLiteral("%1/%2").arg(QLatin1String(Prefix), sys);
        const QVariantMap all = getAll(path);
        if (all.isEmpty())
            continue;
        const QString name = unwrap(all.value(QStringLiteral("name"))).toString();
        const bool touch = unwrap(all.value(QStringLiteral("touch"))).toBool();
        const bool tabletTool = unwrap(all.value(QStringLiteral("tabletTool"))).toBool();
        const bool touchpad = unwrap(all.value(QStringLiteral("touchpad"))).toBool();
        const quint32 vendor = unwrap(all.value(QStringLiteral("vendor"))).toUInt();
        const quint32 product = unwrap(all.value(QStringLiteral("product"))).toUInt();
        if (isDenied(name, touchpad, vendor, product))
            continue;
        if (!touch && !tabletTool)
            continue;
        Digitizer d;
        d.name = name;
        d.vendor = vendor;
        d.product = product;
        d.sysName = sys;
        d.path = path;
        d.r = unwrap(all.value(QStringLiteral("orientationDBus"))).toInt();
        d.ok = true;
        out.append(d);
    }
    return out;
}

Digitizer resolveDigitizer(DigitizerClass kind, const Digitizer *identity)
{
    Digitizer first;
    for (const QString &sys : sysNames()) {
        const QString path = QStringLiteral("%1/%2").arg(QLatin1String(Prefix), sys);
        const QVariantMap all = getAll(path);
        if (all.isEmpty())
            continue;
        const QString name = unwrap(all.value(QStringLiteral("name"))).toString();
        const bool touch = unwrap(all.value(QStringLiteral("touch"))).toBool();
        const bool tabletTool = unwrap(all.value(QStringLiteral("tabletTool"))).toBool();
        const bool touchpad = unwrap(all.value(QStringLiteral("touchpad"))).toBool();
        const quint32 vendor = unwrap(all.value(QStringLiteral("vendor"))).toUInt();
        const quint32 product = unwrap(all.value(QStringLiteral("product"))).toUInt();
        if (isDenied(name, touchpad, vendor, product))
            continue;
        if (kind == DigitizerClass::Finger && !touch)
            continue;
        if (kind == DigitizerClass::Pen && !tabletTool)
            continue;
        Digitizer d;
        d.name = name;
        d.vendor = vendor;
        d.product = product;
        d.sysName = sys;
        d.path = path;
        d.r = unwrap(all.value(QStringLiteral("orientationDBus"))).toInt();
        d.ok = true;
        if (identity && identity->ok && !identity->name.isEmpty()
            && identity->name == d.name && identity->vendor == d.vendor
            && identity->product == d.product)
            return d;
        if (identity && !identity->name.isEmpty()
            && identity->name == d.name && identity->vendor == d.vendor
            && identity->product == d.product)
            return d;
        if (!first.ok)
            first = d;
    }
    return first;
}

bool setOrientation(const QString &path, int r, QString *error)
{
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty InputDevice path");
        return false;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(KWin), path, QString::fromLatin1(PropsIface),
        QStringLiteral("Set"));
    msg << QString::fromLatin1(InputIface)
        << QStringLiteral("orientationDBus")
        << QVariant::fromValue(QDBusVariant(QVariant::fromValue(qint32(r))));
    const QDBusMessage reply = bus.call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    return true;
}

int getOrientation(const QString &path, bool *ok)
{
    if (path.isEmpty()) {
        if (ok)
            *ok = false;
        return 0;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(KWin), path, QString::fromLatin1(PropsIface),
        QStringLiteral("Get"));
    msg << QString::fromLatin1(InputIface) << QStringLiteral("orientationDBus");
    const QDBusMessage reply = bus.call(msg);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    return unwrap(reply.arguments().at(0)).toInt();
}

QString homeProfilePath()
{
    const QString dir = QDir::home().filePath(QStringLiteral(".config/tiltback"));
    if (QDir().mkpath(dir) && QFileInfo(dir).isWritable())
        return dir + QStringLiteral("/home.json");
    return QStringLiteral("/tmp/tiltback-home.json");
}

HomeProfile w620Home()
{
    HomeProfile h;
    h.tHome = QStringLiteral("Rotated90");
    h.rTouch = 8;
    h.rPen = 8;
    h.fingerName = QStringLiteral("STMD1234:00 06CB:1058");
    h.fingerVendor = 0x06cb;
    h.fingerProduct = 0x1058;
    h.penName = QStringLiteral("WCOM0028:00 2D1F:000C Stylus");
    h.penVendor = 0x2d1f;
    h.penProduct = 0x000c;
    return h;
}

HomeProfile loadHome(const QString &dmiProduct, bool allowDmiSeed)
{
    const QString path = homeProfilePath();
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        HomeProfile h;
        h.tHome = o.value(QStringLiteral("tHome")).toString();
        h.rTouch = o.value(QStringLiteral("rTouch")).toInt(8);
        h.rPen = o.value(QStringLiteral("rPen")).toInt(8);
        const QJsonObject finger = o.value(QStringLiteral("finger")).toObject();
        h.fingerName = finger.value(QStringLiteral("name")).toString();
        h.fingerVendor = static_cast<quint32>(finger.value(QStringLiteral("vendor")).toInt());
        h.fingerProduct = static_cast<quint32>(finger.value(QStringLiteral("product")).toInt());
        const QJsonObject pen = o.value(QStringLiteral("pen")).toObject();
        h.penName = pen.value(QStringLiteral("name")).toString();
        h.penVendor = static_cast<quint32>(pen.value(QStringLiteral("vendor")).toInt());
        h.penProduct = static_cast<quint32>(pen.value(QStringLiteral("product")).toInt());
        if (!h.tHome.isEmpty())
            return h;
    }
    if (allowDmiSeed && dmiProduct.contains(QLatin1String("Galaxy Book 10.6")))
        return w620Home();
    return {};
}

bool saveHomeFile(const HomeProfile &home, QString *error)
{
    QJsonObject finger;
    finger.insert(QStringLiteral("name"), home.fingerName);
    finger.insert(QStringLiteral("vendor"), static_cast<int>(home.fingerVendor));
    finger.insert(QStringLiteral("product"), static_cast<int>(home.fingerProduct));
    QJsonObject pen;
    pen.insert(QStringLiteral("name"), home.penName);
    pen.insert(QStringLiteral("vendor"), static_cast<int>(home.penVendor));
    pen.insert(QStringLiteral("product"), static_cast<int>(home.penProduct));
    QJsonObject o;
    o.insert(QStringLiteral("tHome"), home.tHome);
    o.insert(QStringLiteral("rTouch"), home.rTouch);
    o.insert(QStringLiteral("rPen"), home.rPen);
    o.insert(QStringLiteral("finger"), finger);
    o.insert(QStringLiteral("pen"), pen);
    const QByteArray data = QJsonDocument(o).toJson(QJsonDocument::Indented);
    return writeTextFile(homeProfilePath(), data, error);
}

bool persistKcminputrc(const HomeProfile &home, QString *error)
{
    const QString path = QDir::home().filePath(QStringLiteral(".config/kcminputrc"));
    QString text;
    QFile f(path);
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error)
                *error = QStringLiteral("cannot read kcminputrc");
            return false;
        }
        text = QString::fromUtf8(f.readAll());
        f.close();
    }
    if (!home.fingerName.isEmpty())
        upsertOrientation(&text, libinputGroup(home.fingerVendor, home.fingerProduct, home.fingerName),
                          home.rTouch);
    if (!home.penName.isEmpty())
        upsertOrientation(&text, libinputGroup(home.penVendor, home.penProduct, home.penName),
                          home.rPen);
    return writeTextFile(path, text.toUtf8(), error);
}

QString clinicHoldPath()
{
    const QString dir = QDir::home().filePath(QStringLiteral(".cache/tiltback"));
    if (QDir().mkpath(dir) && QFileInfo(dir).isWritable())
        return dir + QStringLiteral("/clinic-hold");
    return QStringLiteral("/tmp/tiltback-clinic-hold");
}

void writeClinicHold()
{
    QFile f(clinicHoldPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(QByteArray::number(QDateTime::currentSecsSinceEpoch()));
}

void clearClinicHold()
{
    QFile::remove(clinicHoldPath());
}

bool clinicHoldActive()
{
    QFileInfo info(clinicHoldPath());
    if (!info.exists())
        return false;
    return info.lastModified().secsTo(QDateTime::currentDateTime()) < 15;
}

int installFollow(const QString &binaryPath, QString *error)
{
    QString resolved = binaryPath;
    const QString packaged = QStringLiteral("/usr/bin/tiltback");
    if (QFileInfo::exists(packaged) && QFileInfo(packaged).isExecutable())
        resolved = packaged;
    const QString persistentDir = QDir::home().filePath(QStringLiteral(".config/systemd/user"));
    QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty())
        runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty())
        runtimeDir += QStringLiteral("/systemd/user");

    const QString body = QStringLiteral(
        "[Unit]\n"
        "Description=TiltBack follow-output residual\n"
        "After=graphical-session.target\n"
        "PartOf=graphical-session.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStart=%1 --follow\n"
        "Restart=on-failure\n"
        "RestartSec=2\n"
        "\n"
        "[Install]\n"
        "WantedBy=graphical-session.target\n")
                             .arg(resolved);
    const QString dropBody = QStringLiteral(
                                 "[Service]\n"
                                 "ExecStart=\n"
                                 "ExecStart=%1 --follow\n")
                                 .arg(resolved);

    const QString persistentUnit = persistentDir + QStringLiteral("/tiltback-follow.service");
    bool wrotePersistent = false;
    if (QDir().mkpath(persistentDir) && QFileInfo(persistentDir).isWritable())
        wrotePersistent = writeTextFile(persistentUnit, body.toUtf8(), nullptr);

    if (!wrotePersistent) {
        if (runtimeDir.isEmpty()) {
            if (error)
                *error = QStringLiteral("cannot write systemd user unit (home RO, no XDG_RUNTIME_DIR)");
            return 1;
        }
        if (QFile::exists(persistentUnit)) {
            const QString dropDir = runtimeDir + QStringLiteral("/tiltback-follow.service.d");
            if (!QDir().mkpath(dropDir) || !writeTextFile(dropDir + QStringLiteral("/override.conf"),
                                                         dropBody.toUtf8(), error))
                return 1;
        } else if (!QDir().mkpath(runtimeDir)
                   || !writeTextFile(runtimeDir + QStringLiteral("/tiltback-follow.service"), body.toUtf8(),
                                     error)) {
            return 1;
        }
    }

    auto run = [](const QStringList &args) {
        QProcess p;
        p.start(QStringLiteral("systemctl"), args);
        p.waitForFinished(10000);
        return p.exitCode();
    };
    run({QStringLiteral("--user"), QStringLiteral("stop"), QStringLiteral("tiltback-w620.service")});
    run({QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("tiltback-w620.service")});
    run({QStringLiteral("--user"), QStringLiteral("mask"), QStringLiteral("--runtime"),
         QStringLiteral("tiltback-w620.service")});
    if (!runtimeDir.isEmpty()) {
        const QString w620Drop = runtimeDir + QStringLiteral("/tiltback-w620.service.d");
        if (QDir().mkpath(w620Drop)) {
            writeTextFile(w620Drop + QStringLiteral("/override.conf"),
                          QByteArray("[Service]\nExecStart=\nExecStart=/bin/true\n"), nullptr);
        }
    }
    run({QStringLiteral("--user"), QStringLiteral("stop"), QStringLiteral("tiltback-follow.service")});
    if (wrotePersistent)
        run({QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("tiltback-follow.service")});
    run({QStringLiteral("--user"), QStringLiteral("daemon-reload")});

    QStringList enable = {QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"),
                          QStringLiteral("tiltback-follow.service")};
    if (!wrotePersistent)
        enable.insert(2, QStringLiteral("--runtime"));
    if (run(enable) != 0) {
        if (run({QStringLiteral("--user"), QStringLiteral("start"), QStringLiteral("tiltback-follow.service")})
            != 0) {
            if (error)
                *error = QStringLiteral("systemctl start tiltback-follow.service failed");
            return 1;
        }
    }
    return 0;
}

} // namespace TiltBack
