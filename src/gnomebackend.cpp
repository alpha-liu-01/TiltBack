#include "gnomebackend.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QProcess>
#include <QThread>
#include <QVariant>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusMetaType>
#include <QtDBus/QDBusVariant>

namespace TiltBack {

struct ApplyMonitor {
    QString connector;
    QString modeId;
    QVariantMap properties;
};

struct ApplyLogical {
    int x = 0;
    int y = 0;
    double scale = 1.0;
    uint transform = 0;
    bool primary = false;
    QList<ApplyMonitor> monitors;
};

QDBusArgument &operator<<(QDBusArgument &arg, const ApplyMonitor &m)
{
    arg.beginStructure();
    arg << m.connector << m.modeId << m.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ApplyMonitor &m)
{
    arg.beginStructure();
    arg >> m.connector >> m.modeId >> m.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const ApplyLogical &l)
{
    arg.beginStructure();
    arg << l.x << l.y << l.scale << l.transform << l.primary << l.monitors;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ApplyLogical &l)
{
    arg.beginStructure();
    arg >> l.x >> l.y >> l.scale >> l.transform >> l.primary >> l.monitors;
    arg.endStructure();
    return arg;
}

} // namespace TiltBack

Q_DECLARE_METATYPE(TiltBack::ApplyMonitor)
Q_DECLARE_METATYPE(QList<TiltBack::ApplyMonitor>)
Q_DECLARE_METATYPE(TiltBack::ApplyLogical)
Q_DECLARE_METATYPE(QList<TiltBack::ApplyLogical>)

namespace TiltBack {
namespace {

const char DisplayService[] = "org.gnome.Mutter.DisplayConfig";
const char DisplayPath[] = "/org/gnome/Mutter/DisplayConfig";
const char DisplayIface[] = "org.gnome.Mutter.DisplayConfig";

struct PhysicalMonitor {
    QString connector;
    QString vendor;
    QString product;
    QString serial;
    QString currentMode;
    bool builtin = false;
};

struct LogicalMonitor {
    int x = 0;
    int y = 0;
    double scale = 1.0;
    uint transform = 0;
    bool primary = false;
    QString connector;
    QString vendor;
    QString product;
    QString serial;
};

struct DisplayState {
    uint serial = 0;
    QVector<PhysicalMonitor> monitors;
    QVector<LogicalMonitor> logicals;
    QVariantMap properties;
    bool ok = false;
};

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2
        && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        return value.mid(1, value.size() - 2);
    }
    return value;
}

bool isEraserName(const QString &name)
{
    return name.contains(QLatin1String("eraser"), Qt::CaseInsensitive);
}

bool isStylusName(const QString &name)
{
    return name.contains(QLatin1String("Stylus"), Qt::CaseInsensitive) && !isEraserName(name);
}

bool looksLikeTablet(const QString &name)
{
    return isStylusName(name) || isEraserName(name)
        || name.contains(QLatin1String("Wacom"), Qt::CaseInsensitive)
        || name.contains(QLatin1String("WCOM"), Qt::CaseInsensitive);
}

bool looksLikeTouch(const QString &name)
{
    return name.contains(QLatin1String("Touchscreen"), Qt::CaseInsensitive)
        || name.contains(QLatin1String("STMD"), Qt::CaseInsensitive);
}

QString kscreenFromMutter(uint transform)
{
    switch (transform & 3u) {
    case 1:
        return QStringLiteral("right");
    case 2:
        return QStringLiteral("inverted");
    case 3:
        return QStringLiteral("left");
    default:
        return QStringLiteral("none");
    }
}

uint mutterFromKscreen(const QString &kscreen)
{
    const QString t = normalizeKscreen(kscreen);
    if (t == QLatin1String("right"))
        return 1;
    if (t == QLatin1String("inverted"))
        return 2;
    if (t == QLatin1String("left"))
        return 3;
    return 0;
}

QString gdctlTransform(const QString &kscreen)
{
    switch (mutterFromKscreen(kscreen)) {
    case 1:
        return QStringLiteral("90");
    case 2:
        return QStringLiteral("180");
    case 3:
        return QStringLiteral("270");
    default:
        return QStringLiteral("normal");
    }
}

QVariantMap asMap(const QVariant &value)
{
    const QVariant v = unwrap(value);
    if (v.canConvert<QVariantMap>())
        return v.toMap();
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(qvariant_cast<QDBusArgument>(v));
    return {};
}

// QDBusArgument overloads beginArray/beginStructure as write (non-const) vs
// read (const). A reply argument is read-only; a non-const call picks the
// write overload, prints "write from a read-only object", and never advances.
QString consumeCurrentModeId(const QDBusArgument &modes)
{
    QString preferred;
    QString first;
    if (modes.currentType() != QDBusArgument::ArrayType)
        return {};
    modes.beginArray();
    int guard = 0;
    while (!modes.atEnd() && guard++ < 64) {
        modes.beginStructure();
        QString id;
        int width = 0;
        int height = 0;
        double refresh = 0;
        double preferredScale = 0;
        modes >> id >> width >> height >> refresh >> preferredScale;
        if (modes.currentType() == QDBusArgument::ArrayType) {
            QList<double> scales;
            modes >> scales;
        }
        QVariantMap props;
        if (modes.currentType() == QDBusArgument::MapType)
            modes >> props;
        modes.endStructure();
        if (first.isEmpty())
            first = id;
        if (props.value(QStringLiteral("is-preferred")).toBool())
            preferred = id;
        if (props.value(QStringLiteral("is-current")).toBool() && !id.isEmpty()) {
            while (!modes.atEnd() && guard++ < 64) {
                modes.beginStructure();
                QString skip;
                int w = 0;
                int h = 0;
                double r = 0;
                double s = 0;
                modes >> skip >> w >> h >> r >> s;
                if (modes.currentType() == QDBusArgument::ArrayType) {
                    QList<double> scales;
                    modes >> scales;
                }
                if (modes.currentType() == QDBusArgument::MapType) {
                    QVariantMap ignored;
                    modes >> ignored;
                }
                modes.endStructure();
            }
            modes.endArray();
            return id;
        }
    }
    modes.endArray();
    return preferred.isEmpty() ? first : preferred;
}

PhysicalMonitor parsePhysical(const QDBusArgument &mon)
{
    PhysicalMonitor out;
    mon.beginStructure();
    if (mon.currentType() == QDBusArgument::StructureType) {
        mon.beginStructure();
        mon >> out.connector >> out.vendor >> out.product >> out.serial;
        mon.endStructure();
    } else {
        mon >> out.connector >> out.vendor >> out.product >> out.serial;
    }
    if (mon.currentType() == QDBusArgument::ArrayType)
        out.currentMode = consumeCurrentModeId(mon);
    if (mon.currentType() == QDBusArgument::MapType) {
        QVariantMap props;
        mon >> props;
        out.builtin = props.value(QStringLiteral("is-builtin")).toBool();
    }
    mon.endStructure();
    return out;
}

LogicalMonitor parseLogical(const QDBusArgument &lm)
{
    LogicalMonitor out;
    lm.beginStructure();
    lm >> out.x >> out.y >> out.scale >> out.transform >> out.primary;
    if (lm.currentType() == QDBusArgument::ArrayType) {
        lm.beginArray();
        int guard = 0;
        if (!lm.atEnd()) {
            lm.beginStructure();
            lm >> out.connector;
            if (lm.currentType() == QDBusArgument::BasicType)
                lm >> out.vendor;
            if (lm.currentType() == QDBusArgument::BasicType)
                lm >> out.product;
            if (lm.currentType() == QDBusArgument::BasicType)
                lm >> out.serial;
            if (lm.currentType() == QDBusArgument::MapType) {
                QVariantMap ignored;
                lm >> ignored;
            }
            lm.endStructure();
            while (!lm.atEnd() && guard++ < 16) {
                lm.beginStructure();
                QString skip;
                lm >> skip;
                int inner = 0;
                while ((lm.currentType() == QDBusArgument::BasicType
                        || lm.currentType() == QDBusArgument::MapType)
                       && inner++ < 8) {
                    QVariant dump;
                    lm >> dump;
                }
                lm.endStructure();
            }
        }
        lm.endArray();
    }
    if (lm.currentType() == QDBusArgument::MapType) {
        QVariantMap ignored;
        lm >> ignored;
    }
    lm.endStructure();
    return out;
}

DisplayState getCurrentState()
{
    DisplayState state;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return state;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(DisplayService), QString::fromLatin1(DisplayPath),
        QString::fromLatin1(DisplayIface), QStringLiteral("GetCurrentState"));
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 8000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 3)
        return state;
    state.serial = reply.arguments().at(0).toUInt();
    if (reply.arguments().size() >= 4)
        state.properties = asMap(reply.arguments().at(3));

    if (reply.arguments().at(1).canConvert<QDBusArgument>()) {
        const QDBusArgument monitors = qvariant_cast<QDBusArgument>(reply.arguments().at(1));
        if (monitors.currentType() == QDBusArgument::ArrayType) {
            monitors.beginArray();
            int guard = 0;
            while (!monitors.atEnd() && guard++ < 16)
                state.monitors.append(parsePhysical(monitors));
            monitors.endArray();
        }
    }

    if (reply.arguments().at(2).canConvert<QDBusArgument>()) {
        const QDBusArgument logicals = qvariant_cast<QDBusArgument>(reply.arguments().at(2));
        if (logicals.currentType() == QDBusArgument::ArrayType) {
            logicals.beginArray();
            int guard = 0;
            while (!logicals.atEnd() && guard++ < 16)
                state.logicals.append(parseLogical(logicals));
            logicals.endArray();
        }
    }
    state.ok = !state.logicals.isEmpty();
    return state;
}

const LogicalMonitor *pickLogical(const DisplayState &state)
{
    const LogicalMonitor *fallback = nullptr;
    for (const LogicalMonitor &lm : state.logicals) {
        if (!fallback)
            fallback = &lm;
        if (isBuiltinOutput(lm.connector))
            return &lm;
    }
    for (const PhysicalMonitor &mon : state.monitors) {
        if (!mon.builtin)
            continue;
        for (const LogicalMonitor &lm : state.logicals) {
            if (lm.connector == mon.connector)
                return &lm;
        }
    }
    return fallback;
}

QString modeForConnector(const DisplayState &state, const QString &connector)
{
    for (const PhysicalMonitor &mon : state.monitors) {
        if (mon.connector == connector)
            return mon.currentMode;
    }
    return {};
}

QString tabletSchemaPath(quint32 vendor, quint32 product)
{
    return QStringLiteral("/org/gnome/desktop/peripherals/tablet/%1/")
        .arg(vidPid(vendor, product));
}

QString touchSchemaPath(quint32 vendor, quint32 product)
{
    return QStringLiteral("/org/gnome/desktop/peripherals/touchscreens/%1/")
        .arg(vidPid(vendor, product));
}

QString userUdevRulesPath()
{
    return QDir::home().filePath(QStringLiteral(".config/tiltback/61-tiltback.rules"));
}

QString etcUdevRulesPath()
{
    return QStringLiteral("/etc/udev/rules.d/61-tiltback.rules");
}

QString matrixForResidual(int r)
{
    switch (r) {
    case 1:
        return QStringLiteral("0 -1 1 1 0 0");
    case 2:
        return QStringLiteral("0 1 0 -1 0 1");
    case 4:
    case 8:
        return QStringLiteral("-1 0 1 0 -1 1");
    default:
        return QStringLiteral("1 0 0 0 1 0");
    }
}

int classifyMatrix(const QString &text)
{
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() < 6)
        return 0;
    float m[6];
    for (int i = 0; i < 6; ++i)
        m[i] = parts.at(i).toFloat();
    const float cand[][6] = {
        {1, 0, 0, 0, 1, 0},
        {0, -1, 1, 1, 0, 0},
        {0, 1, 0, -1, 0, 1},
        {-1, 0, 1, 0, -1, 1},
    };
    const int rs[] = {0, 1, 2, 8};
    int best = 0;
    float bestD = 1e9f;
    for (int i = 0; i < 4; ++i) {
        float d = 0;
        for (int j = 0; j < 6; ++j) {
            const float e = m[j] - cand[i][j];
            d += e * e;
        }
        if (d < bestD) {
            bestD = d;
            best = rs[i];
        }
    }
    return best;
}

QMap<QString, QString> readUdevRuleMap(const QString &path)
{
    QMap<QString, QString> out;
    const QString body = readText(path);
    const QStringList lines = body.split(QLatin1Char('\n'));
    QString pending;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("# tiltback:"))) {
            pending = line.mid(11).trimmed();
            continue;
        }
        if (pending.isEmpty())
            continue;
        const int key = line.indexOf(QLatin1String("LIBINPUT_CALIBRATION_MATRIX}="));
        if (key >= 0) {
            const int start = line.indexOf(QLatin1Char('"'), key);
            const int end = start >= 0 ? line.indexOf(QLatin1Char('"'), start + 1) : -1;
            if (start >= 0 && end > start)
                out.insert(pending, line.mid(start + 1, end - start - 1));
        }
        pending.clear();
    }
    return out;
}

bool writeUdevRuleMap(const QMap<QString, QString> &rules, QString *error)
{
    const QString path = userUdevRulesPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString body = QStringLiteral(
        "# TiltBack residual. Touch is constant (Mutter composes T). "
        "Pen is composed with ΔT (Mutter does not rotate tablet-tools).\n");
    for (auto it = rules.begin(); it != rules.end(); ++it) {
        if (it.value().isEmpty() || classifyMatrix(it.value()) == 0)
            continue;
        const QString idEnv = (looksLikeTablet(it.key()) && !looksLikeTouch(it.key()))
            ? QStringLiteral("ENV{ID_INPUT_TABLET}==\"1\"")
            : QStringLiteral("ENV{ID_INPUT_TOUCHSCREEN}==\"1\"");
        body += QStringLiteral("# tiltback:%1\n").arg(it.key());
        body += QStringLiteral(
                    "ACTION==\"add|change\", KERNEL==\"event*\", %1, ATTRS{name}==\"%2\", "
                    "ENV{LIBINPUT_CALIBRATION_MATRIX}=\"%3\"\n")
                    .arg(idEnv, it.key(), it.value());
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    f.write(body.toUtf8());
    return true;
}

QString packagedRebindScript()
{
    const QStringList candidates = {
        QStringLiteral("/usr/libexec/tiltback/rebind-hid.sh"),
        QStringLiteral("/usr/lib/tiltback/rebind-hid.sh"),
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

QString rebindScriptPath()
{
    const QString packaged = packagedRebindScript();
    if (!packaged.isEmpty())
        return packaged;
    return QDir::home().filePath(QStringLiteral(".config/tiltback/rebind-hid.sh"));
}

QString userRebindStampPath()
{
    return QDir::home().filePath(QStringLiteral(".config/tiltback/last-rebind-done"));
}

QString packagedRebindStampPath()
{
    return QStringLiteral("/run/tiltback/last-rebind-done");
}

QString rebindRequestPath()
{
    return QStringLiteral("/run/tiltback/rebind-request");
}

QByteArray currentRulesStamp()
{
    QFile f(userUdevRulesPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
}

bool compositorPickedCurrentRules()
{
    const QByteArray want = currentRulesStamp();
    if (want.isEmpty())
        return false;
    const QStringList stamps = {packagedRebindStampPath(), userRebindStampPath()};
    for (const QString &path : stamps) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.readAll().trimmed() == want)
            return true;
    }
    return false;
}

bool requestPackagedRebind()
{
    QDir().mkpath(QStringLiteral("/run/tiltback"));
    QFile f(rebindRequestPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    f.write(currentRulesStamp());
    f.write("\n");
    return true;
}

QString userRebindScriptPath()
{
    return QDir::home().filePath(QStringLiteral(".config/tiltback/rebind-hid.sh"));
}

bool writeRebindScript(QString *error)
{
    const QString path = userRebindScriptPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString body = QStringLiteral(
        "#!/bin/sh\n"
        "# Mutter keeps evdev open. udevadm trigger does not reload calibration.\n"
        "set -eu\n"
        "RULES='%1'\n"
        "STAMP='%2'\n"
        "udevadm control --reload || true\n"
        "rebound=0\n"
        "for hid in /sys/bus/hid/devices/*; do\n"
        "  [ -e \"$hid/driver\" ] || continue\n"
        "  id=$(basename \"$hid\")\n"
        "  case \"$id\" in\n"
        "    *:04E8:A00A.*) continue ;;\n"
        "    *:06CB:1058.*|*:2D1F:000C.*) ;;\n"
        "    *) continue ;;\n"
        "  esac\n"
        "  drv=$(readlink -f \"$hid/driver\")\n"
        "  echo \"$id\" > \"$drv/unbind\"\n"
        "  echo \"$id\" > \"$drv/bind\"\n"
        "  rebound=$((rebound+1))\n"
        "done\n"
        "[ \"$rebound\" -gt 0 ]\n"
        "sha256sum \"$RULES\" | awk '{print $1}' > \"$STAMP\"\n")
                             .arg(userUdevRulesPath(), userRebindStampPath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    f.write(body.toUtf8());
    f.close();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                      | QFile::ReadGroup | QFile::ReadOther);
    return true;
}

QString rebindServiceSrcPath()
{
    return QDir::home().filePath(QStringLiteral(".config/tiltback/tiltback-rebind.service"));
}

QString rebindPathSrcPath()
{
    return QDir::home().filePath(QStringLiteral(".config/tiltback/tiltback-rebind.path"));
}

bool writeRebindUnits()
{
    const QString service = QStringLiteral(
        "[Unit]\n"
        "Description=TiltBack HID rebind so Mutter reopens digitizers\n"
        "\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=%1\n")
                                .arg(rebindScriptPath());
    const QString path = QStringLiteral(
        "[Unit]\n"
        "Description=TiltBack HID rebind when residual rules change\n"
        "\n"
        "[Path]\n"
        "PathModified=%1\n"
        "PathChanged=%1\n"
        "Unit=tiltback-rebind.service\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n")
                             .arg(userUdevRulesPath());
    QDir().mkpath(QFileInfo(rebindServiceSrcPath()).absolutePath());
    auto write = [](const QString &file, const QString &body) {
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        f.write(body.toUtf8());
        return true;
    };
    return write(rebindServiceSrcPath(), service) && write(rebindPathSrcPath(), path);
}

bool rebindPathInstalled()
{
    const QStringList units = {
        QStringLiteral("/usr/lib/systemd/system/tiltback-rebind.path"),
        QStringLiteral("/lib/systemd/system/tiltback-rebind.path"),
        QStringLiteral("/etc/systemd/system/tiltback-rebind.path"),
    };
    for (const QString &p : units) {
        if (QFileInfo::exists(p))
            return true;
    }
    return false;
}

bool udevRuleInstalled();

QString udevInstallHint()
{
    QString text = QStringLiteral(
        "udev updated the property, but Mutter still has the old evdev fds. "
        "`udevadm trigger` does not reopen them. Rule is staged at %1. ")
                       .arg(userUdevRulesPath());
    if (!udevRuleInstalled()) {
        text += QStringLiteral(
            "If /etc/udev/rules.d/61-tiltback.rules is not a symlink to that file yet:\n"
            "sudo mkdir -p /etc/udev/rules.d\n"
            "sudo ln -sf %1 /etc/udev/rules.d/61-tiltback.rules\n")
                    .arg(userUdevRulesPath());
    }
    if (rebindPathInstalled()) {
        text += QStringLiteral(
            "The packaged tiltback-rebind.path should reopen the devices. "
            "If it did not: systemctl status tiltback-rebind.path");
        return text;
    }
    text += QStringLiteral(
                "For follow to rotate the stylus without a password each time, "
                "install the GNOME helper package (tiltback-gnome / tiltback-gnome.rpm) "
                "or enable the user units once:\n"
                "sudo cp %1 /etc/systemd/system/tiltback-rebind.service\n"
                "sudo cp %2 /etc/systemd/system/tiltback-rebind.path\n"
                "sudo systemctl daemon-reload\n"
                "sudo systemctl enable --now tiltback-rebind.path\n"
                "Or reopen once from SSH / Polkit:\n"
                "sudo %3")
                .arg(rebindServiceSrcPath(), rebindPathSrcPath(), rebindScriptPath());
    return text;
}

bool udevRuleInstalled()
{
    const QFileInfo etc(etcUdevRulesPath());
    if (!etc.exists() && !etc.isSymLink())
        return false;
    const QString dest = etc.canonicalFilePath();
    return dest == QFileInfo(userUdevRulesPath()).canonicalFilePath() || etc.exists();
}

bool runElevated(const QString &bin, const QStringList &args, int timeoutMs)
{
    if (QStandardPaths::findExecutable(bin).isEmpty() && !QFileInfo::exists(bin))
        return false;
    QProcess proc;
    proc.start(bin, args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        return false;
    }
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool tryRebindHid()
{
    const QString script = rebindScriptPath();
    if (!QFileInfo::exists(script))
        return false;
    return runElevated(QStringLiteral("sudo"), {QStringLiteral("-n"), script}, 15000);
}

void requestRebindDialog()
{
    const QString script = rebindScriptPath();
    const QString pk = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pk.isEmpty() || !QFileInfo::exists(script))
        return;
    QProcess::startDetached(pk, {script});
}

QString eventSysPath(const QString &eventName);
QHash<QString, QString> udevProperties(const QString &sysPath);

int residualFromUdev(const Digitizer &dev)
{
    if (!dev.sysName.isEmpty()) {
        const QString sys = eventSysPath(dev.sysName);
        const QHash<QString, QString> props = udevProperties(sys);
        const QString live = props.value(QStringLiteral("LIBINPUT_CALIBRATION_MATRIX"));
        if (!live.isEmpty())
            return classifyMatrix(live);
    }
    const QMap<QString, QString> rules = readUdevRuleMap(userUdevRulesPath());
    if (rules.contains(dev.name))
        return classifyMatrix(rules.value(dev.name));
    return 0;
}

bool runGsettings(const QStringList &args, QString *out, QString *error)
{
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
    if (bin.isEmpty()) {
        if (error)
            *error = QStringLiteral("gsettings not found");
        return false;
    }
    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("gsettings timed out");
        return false;
    }
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    const QString text = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    if (out)
        *out = text;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("gsettings failed") : err;
        return false;
    }
    return true;
}

QHash<QString, QString> udevProperties(const QString &sysPath)
{
    QHash<QString, QString> props;
    const QString dev = readText(sysPath + QStringLiteral("/dev"));
    if (!dev.isEmpty()) {
        const QString dataPath = QStringLiteral("/run/udev/data/c%1").arg(dev);
        const QString body = readText(dataPath);
        const QStringList lines = body.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (!line.startsWith(QLatin1String("E:")))
                continue;
            const int eq = line.indexOf(QLatin1Char('='), 2);
            if (eq < 0)
                continue;
            props.insert(line.mid(2, eq - 2), unquote(line.mid(eq + 1)));
        }
        if (!props.isEmpty())
            return props;
    }

    const QString udevadm = QStandardPaths::findExecutable(QStringLiteral("udevadm"));
    if (udevadm.isEmpty())
        return props;
    QProcess proc;
    proc.setProgram(udevadm);
    proc.setArguments({QStringLiteral("info"), QStringLiteral("--query=property"),
                       QStringLiteral("--path"), sysPath});
    proc.start();
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        return props;
    }
    const QStringList lines =
        QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        props.insert(line.left(eq), unquote(line.mid(eq + 1)));
    }
    return props;
}

struct UdevNode {
    Digitizer digitizer;
    bool touch = false;
    bool tablet = false;
    bool eraser = false;
};

QString eventSysPath(const QString &eventName)
{
    const QString link = QStringLiteral("/sys/class/input/%1").arg(eventName);
    const QFileInfo info(link);
    if (!info.exists() && !info.isSymLink())
        return {};
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

UdevNode nodeFromIdentity(const QString &name, quint32 vendor, quint32 product,
                          const QString &eventName)
{
    UdevNode node;
    const QString sys = eventSysPath(eventName);
    const QHash<QString, QString> props = sys.isEmpty() ? QHash<QString, QString>()
                                                        : udevProperties(sys);
    const bool touchpad = props.value(QStringLiteral("ID_INPUT_TOUCHPAD")) == QLatin1String("1")
        || name.contains(QLatin1String("Touchpad"), Qt::CaseInsensitive);
    const bool touch = props.value(QStringLiteral("ID_INPUT_TOUCHSCREEN")) == QLatin1String("1")
        || looksLikeTouch(name);
    const bool tablet = props.value(QStringLiteral("ID_INPUT_TABLET")) == QLatin1String("1")
        || looksLikeTablet(name);
    const bool pad = props.value(QStringLiteral("ID_INPUT_TABLET_PAD")) == QLatin1String("1")
        && !looksLikeTablet(name);
    if (pad || (!touch && !tablet) || isDenied(name, touchpad, vendor, product))
        return node;
    node.touch = touch && !looksLikeTablet(name);
    node.tablet = looksLikeTablet(name) || (tablet && !touch);
    node.eraser = isEraserName(name);
    if (!node.touch && !node.tablet)
        return {};
    node.digitizer.name = name;
    node.digitizer.vendor = vendor;
    node.digitizer.product = product;
    node.digitizer.sysName = eventName;
    node.digitizer.path = sys;
    node.digitizer.ok = true;
    node.digitizer.r = residualFromUdev(node.digitizer);
    return node;
}

QVector<UdevNode> scanProcInput()
{
    QVector<UdevNode> out;
    const QString body = readText(QStringLiteral("/proc/bus/input/devices"));
    if (body.isEmpty())
        return out;
    QString name;
    quint32 vendor = 0;
    quint32 product = 0;
    QString eventName;
    auto flush = [&]() {
        if (!eventName.isEmpty() && !name.isEmpty()) {
            const UdevNode node = nodeFromIdentity(name, vendor, product, eventName);
            if (node.digitizer.ok)
                out.append(node);
        }
        name.clear();
        vendor = 0;
        product = 0;
        eventName.clear();
    };
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.isEmpty()) {
            flush();
            continue;
        }
        if (line.startsWith(QLatin1String("I:"))) {
            const QRegularExpression ve(QStringLiteral("Vendor=([0-9a-fA-F]+)"));
            const QRegularExpression pe(QStringLiteral("Product=([0-9a-fA-F]+)"));
            const auto vm = ve.match(line);
            const auto pm = pe.match(line);
            if (vm.hasMatch())
                vendor = vm.captured(1).toUInt(nullptr, 16);
            if (pm.hasMatch())
                product = pm.captured(1).toUInt(nullptr, 16);
        } else if (line.startsWith(QLatin1String("N:"))) {
            const int q1 = line.indexOf(QLatin1Char('"'));
            const int q2 = line.lastIndexOf(QLatin1Char('"'));
            if (q1 >= 0 && q2 > q1)
                name = line.mid(q1 + 1, q2 - q1 - 1);
        } else if (line.startsWith(QLatin1String("H:"))) {
            const QRegularExpression ev(QStringLiteral("\\bevent\\d+\\b"));
            const auto em = ev.match(line);
            if (em.hasMatch())
                eventName = em.captured(0);
        }
    }
    flush();
    return out;
}

QVector<UdevNode> scanSysfsEvents()
{
    QVector<UdevNode> out;
    const QDir inputDir(QStringLiteral("/sys/class/input"));
    const QStringList names = inputDir.entryList(QStringList() << QStringLiteral("event*"),
                                                 QDir::AllEntries | QDir::System | QDir::Hidden
                                                     | QDir::NoDotAndDotDot);
    for (const QString &eventName : names) {
        const QString sys = eventSysPath(eventName);
        if (sys.isEmpty())
            continue;
        QString name = readText(sys + QStringLiteral("/device/name"));
        if (name.isEmpty())
            name = readText(sys + QStringLiteral("/name"));
        quint32 vendor = readText(sys + QStringLiteral("/device/id/vendor")).toUInt(nullptr, 16);
        quint32 product = readText(sys + QStringLiteral("/device/id/product")).toUInt(nullptr, 16);
        const UdevNode node = nodeFromIdentity(name, vendor, product, eventName);
        if (node.digitizer.ok)
            out.append(node);
    }
    return out;
}

QVector<UdevNode> scanUdevNodes()
{
    const QVector<UdevNode> fromProc = scanProcInput();
    if (!fromProc.isEmpty())
        return fromProc;
    return scanSysfsEvents();
}

bool sameIdentity(const Digitizer &a, const Digitizer &b)
{
    return !a.name.isEmpty() && a.name == b.name && a.vendor == b.vendor && a.product == b.product;
}

void registerDisplayTypes()
{
    static bool done = false;
    if (done)
        return;
    qDBusRegisterMetaType<ApplyMonitor>();
    qDBusRegisterMetaType<QList<ApplyMonitor>>();
    qDBusRegisterMetaType<ApplyLogical>();
    qDBusRegisterMetaType<QList<ApplyLogical>>();
    done = true;
}

OutputInfo readViaGdctl()
{
    OutputInfo info;
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("gdctl"));
    if (bin.isEmpty())
        return info;
    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments({QStringLiteral("show")});
    proc.start();
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        return info;
    }
    const QString text = QString::fromUtf8(proc.readAllStandardOutput());
    QString connector;
    QString transform;
    const QRegularExpression connRe(QStringLiteral("\\b(eDP\\S*|DSI\\S*|LVDS\\S*)\\b"));
    const QRegularExpression tRe(
        QStringLiteral("transform:\\s*(normal|90|180|270|none|left|right|inverted)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto conn = connRe.match(text);
    if (conn.hasMatch())
        connector = conn.captured(1);
    const auto tr = tRe.match(text);
    if (tr.hasMatch())
        transform = tr.captured(1).toLower();
    if (transform == QLatin1String("90"))
        transform = QStringLiteral("right");
    else if (transform == QLatin1String("180"))
        transform = QStringLiteral("inverted");
    else if (transform == QLatin1String("270"))
        transform = QStringLiteral("left");
    else if (transform == QLatin1String("normal"))
        transform = QStringLiteral("none");
    info.name = connector;
    info.tKscreen = normalizeKscreen(transform);
    info.tKwin = kwinNameFromKscreen(info.tKscreen);
    info.ok = !info.tKscreen.isEmpty();
    return info;
}

bool waitForRebindStamp(int tries, int sleepMs)
{
    for (int i = 0; i < tries; ++i) {
        if (compositorPickedCurrentRules())
            return true;
        QThread::msleep(sleepMs);
    }
    return compositorPickedCurrentRules();
}

bool applyUdevMatrix(const Digitizer &dev, const QString &matrix, QString *error)
{
    QMap<QString, QString> rules = readUdevRuleMap(userUdevRulesPath());
    auto applyName = [&](const QString &name) {
        if (classifyMatrix(matrix) == 0)
            rules.remove(name);
        else
            rules.insert(name, matrix);
    };
    applyName(dev.name);
    if (looksLikeTablet(dev.name)) {
        for (const UdevNode &node : scanUdevNodes()) {
            if (node.digitizer.vendor == dev.vendor && node.digitizer.product == dev.product
                && !isDenied(node.digitizer.name, false, node.digitizer.vendor, node.digitizer.product))
                applyName(node.digitizer.name);
        }
    }
    if (!writeUdevRuleMap(rules, error))
        return false;
    writeRebindScript(nullptr);
    writeRebindUnits();
    const bool asked = requestPackagedRebind();
    if (compositorPickedCurrentRules())
        return true;
    if ((rebindPathInstalled() || asked) && waitForRebindStamp(20, 250))
        return true;
    if (!udevRuleInstalled()) {
        if (error)
            *error = udevInstallHint();
        return false;
    }
    if (tryRebindHid() && waitForRebindStamp(8, 250))
        return true;
    if (!rebindPathInstalled())
        requestRebindDialog();
    if (error)
        *error = udevInstallHint();
    return false;
}

} // namespace
} // namespace TiltBack

namespace TiltBack {

GnomeBackend::GnomeBackend(QObject *parent)
    : OrientationBackend(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    registerDisplayTypes();
    bindBuiltinOutputs();
}

QString GnomeBackend::inputBackendLabelFor(DigitizerClass kind) const
{
    if (kind == DigitizerClass::Finger)
        return QStringLiteral("yes (udev constant; Mutter follows T)");
    return QStringLiteral("yes (udev ∘ ΔT; Mutter skips tablet T)");
}

QString GnomeBackend::pictureSource(const OutputInfo &out) const
{
    QString src = out.name.isEmpty()
        ? QStringLiteral("Mutter GetCurrentState (live) + DMI + DRM")
        : QStringLiteral("Mutter GetCurrentState (live, %1) + DMI + DRM").arg(out.name);
    if (QFile::exists(QDir::home().filePath(QStringLiteral(".config/monitors.xml"))))
        src += QStringLiteral(" + monitors.xml");
    return src;
}

QString GnomeBackend::inputSource(const Digitizer &dev) const
{
    if (looksLikeTablet(dev.name) && !looksLikeTouch(dev.name)) {
        return QStringLiteral(
                   "udev LIBINPUT composed with ΔT (Mutter does not rotate tablet-tools with T; "
                   "%1 is not identity); HID rebind after each pose")
            .arg(dev.sysName.isEmpty() ? QStringLiteral("eventN") : dev.sysName);
    }
    return QStringLiteral(
               "constant udev LIBINPUT (Mutter already composes T onto touch; "
               "%1 is not identity); HID rebind only when R changes")
        .arg(dev.sysName.isEmpty() ? QStringLiteral("eventN") : dev.sysName);
}

OutputInfo GnomeBackend::readOutput()
{
    const DisplayState state = getCurrentState();
    const LogicalMonitor *lm = pickLogical(state);
    if (lm) {
        OutputInfo info;
        info.name = lm->connector;
        info.tKscreen = kscreenFromMutter(lm->transform);
        info.tKwin = kwinNameFromKscreen(info.tKscreen);
        info.ok = !info.tKscreen.isEmpty();
        if (info.ok)
            return info;
    }
    return readViaGdctl();
}

bool GnomeBackend::setOutput(const QString &kscreen, QString *error)
{
    return applyTransform(kscreen, false, error);
}

bool GnomeBackend::commitOutput(QString *error)
{
    const QString t = m_lastKscreen.isEmpty() ? readOutput().tKscreen : m_lastKscreen;
    if (t.isEmpty()) {
        if (error)
            *error = QStringLiteral("no transform to persist");
        return false;
    }
    return applyTransform(t, true, error);
}

bool GnomeBackend::applyTransform(const QString &kscreen, bool persistent, QString *error)
{
    const QString want = normalizeKscreen(kscreen);
    if (want.isEmpty()) {
        if (error)
            *error = QStringLiteral("unknown transform %1").arg(kscreen);
        return false;
    }
    OutputInfo live = readOutput();
    QString name = live.name;
    if (name.isEmpty())
        name = QStringLiteral("eDP-1");
    QString gdctlErr;
    if (applyViaGdctl(name, want, persistent, &gdctlErr)) {
        const OutputInfo now = readOutput();
        if (now.ok && now.tKscreen == want) {
            m_lastKscreen = want;
            return true;
        }
        if (error)
            *error = QStringLiteral("gdctl did not change T to %1").arg(want);
        return false;
    }
    QString dbusErr;
    if (applyViaDisplayConfig(name, want, persistent, &dbusErr)) {
        const OutputInfo now = readOutput();
        if (now.ok && now.tKscreen == want) {
            m_lastKscreen = want;
            return true;
        }
        if (error)
            *error = QStringLiteral("ApplyMonitorsConfig did not change T to %1").arg(want);
        return false;
    }
    if (error)
        *error = gdctlErr.isEmpty() ? dbusErr : gdctlErr;
    return false;
}

bool GnomeBackend::applyViaGdctl(const QString &connector, const QString &kscreen, bool persistent,
                                 QString *error)
{
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("gdctl"));
    if (bin.isEmpty()) {
        if (error)
            *error = QStringLiteral("gdctl not found");
        return false;
    }
    QStringList args = {QStringLiteral("set")};
    if (persistent)
        args.append(QStringLiteral("--persistent"));
    args << QStringLiteral("--logical-monitor") << QStringLiteral("--primary")
         << QStringLiteral("--monitor") << connector << QStringLiteral("--transform")
         << gdctlTransform(kscreen);
    QProcess proc;
    proc.setProgram(bin);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForFinished(15000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("gdctl timed out");
        return false;
    }
    const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString combined = err.isEmpty() ? out : err;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error)
            *error = combined.isEmpty() ? QStringLiteral("gdctl failed") : combined;
        return false;
    }
    return true;
}

bool GnomeBackend::applyViaDisplayConfig(const QString &connector, const QString &kscreen,
                                         bool persistent, QString *error)
{
    registerDisplayTypes();
    const DisplayState state = getCurrentState();
    if (!state.ok) {
        if (error)
            *error = QStringLiteral("GetCurrentState failed");
        return false;
    }
    QList<ApplyLogical> logicals;
    for (const LogicalMonitor &lm : state.logicals) {
        ApplyLogical out;
        out.x = lm.x;
        out.y = lm.y;
        out.scale = lm.scale;
        out.transform = (lm.connector == connector) ? mutterFromKscreen(kscreen) : lm.transform;
        out.primary = lm.primary;
        ApplyMonitor mon;
        mon.connector = lm.connector;
        mon.modeId = modeForConnector(state, lm.connector);
        if (mon.modeId.isEmpty()) {
            if (error)
                *error = QStringLiteral("no current mode for %1").arg(lm.connector);
            return false;
        }
        out.monitors.append(mon);
        logicals.append(out);
    }
    if (logicals.isEmpty()) {
        if (error)
            *error = QStringLiteral("no logical monitors");
        return false;
    }

    QVariantMap props;
    if (state.properties.contains(QStringLiteral("layout-mode")))
        props.insert(QStringLiteral("layout-mode"), state.properties.value(QStringLiteral("layout-mode")));

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(DisplayService), QString::fromLatin1(DisplayPath),
        QString::fromLatin1(DisplayIface), QStringLiteral("ApplyMonitorsConfig"));
    msg << state.serial << uint(persistent ? 2 : 1) << QVariant::fromValue(logicals)
        << QVariant::fromValue(props);
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 15000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    return true;
}

QVector<Digitizer> GnomeBackend::listDigitizers()
{
    QVector<Digitizer> out;
    const QVector<UdevNode> nodes = scanUdevNodes();
    for (const UdevNode &node : nodes) {
        Digitizer d = node.digitizer;
        bool ok = false;
        d.r = getResidual(d, &ok);
        if (!ok)
            d.r = 0;
        out.append(d);
    }
    return out;
}

Digitizer GnomeBackend::resolve(DigitizerClass kind, const Digitizer *identity)
{
    const QVector<UdevNode> nodes = scanUdevNodes();
    Digitizer first;
    Digitizer stylus;
    Digitizer picked;
    for (const UdevNode &node : nodes) {
        if (kind == DigitizerClass::Finger) {
            if (!node.touch)
                continue;
        } else {
            if (!node.tablet || node.eraser)
                continue;
        }
        if (identity && identity->ok && sameIdentity(*identity, node.digitizer)) {
            picked = node.digitizer;
            break;
        }
        if (identity && !identity->name.isEmpty() && sameIdentity(*identity, node.digitizer)) {
            picked = node.digitizer;
            break;
        }
        if (!first.ok)
            first = node.digitizer;
        if (kind == DigitizerClass::Pen && !stylus.ok && isStylusName(node.digitizer.name))
            stylus = node.digitizer;
    }
    if (!picked.ok)
        picked = (kind == DigitizerClass::Pen && stylus.ok) ? stylus : first;
    if (picked.ok) {
        bool ok = false;
        picked.r = getResidual(picked, &ok);
        if (!ok)
            picked.r = 0;
    }
    return picked;
}

void GnomeBackend::bindBuiltinOutputs()
{
    QString vendor = QStringLiteral("unknown");
    QString product = QStringLiteral("unknown");
    QString serial = QStringLiteral("unknown");
    const DisplayState state = getCurrentState();
    const LogicalMonitor *lm = pickLogical(state);
    if (lm) {
        if (!lm->vendor.isEmpty())
            vendor = lm->vendor;
        if (!lm->product.isEmpty())
            product = lm->product;
        if (!lm->serial.isEmpty())
            serial = lm->serial;
    }
    const QString value = QStringLiteral("['%1', '%2', '%3']").arg(vendor, product, serial);
    const QVector<UdevNode> nodes = scanUdevNodes();
    for (const UdevNode &node : nodes) {
        const QString schema = node.tablet
            ? QStringLiteral("org.gnome.desktop.peripherals.tablet:%1")
                  .arg(tabletSchemaPath(node.digitizer.vendor, node.digitizer.product))
            : QStringLiteral("org.gnome.desktop.peripherals.touchscreen:%1")
                  .arg(touchSchemaPath(node.digitizer.vendor, node.digitizer.product));
        runGsettings({QStringLiteral("set"), schema, QStringLiteral("output"), value}, nullptr, nullptr);
    }
}

bool GnomeBackend::setResidual(const Digitizer &dev, int r, QString *error)
{
    if (r != 0 && r != 1 && r != 2 && r != 4 && r != 8) {
        if (error)
            *error = QStringLiteral("unsupported residual %1").arg(r);
        return false;
    }
    if (looksLikeTablet(dev.name) && !looksLikeTouch(dev.name)) {
        const QString schema = QStringLiteral("org.gnome.desktop.peripherals.tablet:%1")
                                   .arg(tabletSchemaPath(dev.vendor, dev.product));
        runGsettings({QStringLiteral("set"), schema, QStringLiteral("left-handed"),
                      QStringLiteral("false")},
                     nullptr, nullptr);
    }
    return applyUdevMatrix(dev, matrixForResidual(r), error);
}

int GnomeBackend::getResidual(const Digitizer &dev, bool *ok)
{
    if (ok)
        *ok = true;
    return residualFromUdev(dev);
}

bool GnomeBackend::stampFollow(const HomeProfile &home, QString *error)
{
    if (clinicHoldActive())
        return true;
    Digitizer wantF;
    wantF.name = home.fingerName;
    wantF.vendor = home.fingerVendor;
    wantF.product = home.fingerProduct;
    wantF.ok = !home.fingerName.isEmpty();
    Digitizer wantP;
    wantP.name = home.penName;
    wantP.vendor = home.penVendor;
    wantP.product = home.penProduct;
    wantP.ok = !home.penName.isEmpty();
    Digitizer finger = resolve(DigitizerClass::Finger, wantF.ok ? &wantF : nullptr);
    Digitizer pen = resolve(DigitizerClass::Pen, wantP.ok ? &wantP : nullptr);
    const OutputInfo out = readOutput();
    const QString tNow = out.ok ? out.tKscreen : QStringLiteral("none");
    const QString tHome = home.tHome.isEmpty() ? QStringLiteral("inverted") : home.tHome;
    auto one = [&](const Digitizer &dev, int homeR, bool composeWithT) {
        if (!dev.ok)
            return true;
        const int want = composeWithT ? followResidual(tNow, tHome, homeR) : homeR;
        bool liveOk = false;
        const int now = getResidual(dev, &liveOk);
        const bool sameInvert = (want == 4 || want == 8) && (now == 4 || now == 8);
        if (liveOk && (now == want || sameInvert))
            return true;
        return applyUdevMatrix(dev, matrixForResidual(want), error);
    };
    return one(finger, home.rTouch, false) && one(pen, home.rPen, true);
}

void GnomeBackend::watchPose()
{
    if (m_watching)
        return;
    m_watching = true;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(DisplayService), QString::fromLatin1(DisplayPath),
                QString::fromLatin1(DisplayIface), QStringLiteral("MonitorsChanged"), this,
                SIGNAL(poseChanged()));
    const QString cfg = QDir::home().filePath(QStringLiteral(".config"));
    if (QDir(cfg).exists())
        m_watcher->addPath(cfg);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        emit poseChanged();
    });
}

} // namespace TiltBack
