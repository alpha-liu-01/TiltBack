#include "backend.h"
#include "clinicmodel.h"
#include "followengine.h"
#include "kwininput.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <cstdio>
#include <cstring>

namespace {

bool hasArg(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0)
            return true;
    }
    return false;
}

void setAppIdentity(QCoreApplication &app)
{
    app.setOrganizationName(QStringLiteral("TiltBack"));
    app.setOrganizationDomain(QStringLiteral("tiltback.org"));
    app.setApplicationName(QStringLiteral("TiltBack"));
}

} // namespace

int main(int argc, char *argv[])
{
    if (hasArg(argc, argv, "--follow")) {
#ifdef TILTBACK_X11
        if (TiltBack::sessionLooksX11()) {
            QGuiApplication app(argc, argv);
            setAppIdentity(app);
            TiltBack::FollowEngine follow;
            if (follow.start() != 0)
                return 1;
            return app.exec();
        }
#endif
        QCoreApplication app(argc, argv);
        setAppIdentity(app);
        TiltBack::FollowEngine follow;
        if (follow.start() != 0)
            return 1;
        return app.exec();
    }

    if (hasArg(argc, argv, "--install-follow")) {
        QCoreApplication app(argc, argv);
        setAppIdentity(app);
        QString err;
        const int rc = TiltBack::installFollow(QCoreApplication::applicationFilePath(), &err);
        if (rc != 0) {
            std::fprintf(stderr, "%s\n", qPrintable(err));
            return rc;
        }
        return 0;
    }

    if (hasArg(argc, argv, "--install-greeter")) {
        QCoreApplication app(argc, argv);
        setAppIdentity(app);
        QString err;
        const int rc = TiltBack::installGreeter(&err);
        if (rc != 0) {
            std::fprintf(stderr, "%s\n", qPrintable(err));
            return rc;
        }
        return 0;
    }

    if (hasArg(argc, argv, "--save-home")) {
        QCoreApplication app(argc, argv);
        setAppIdentity(app);
        ClinicModel clinic;
        clinic.saveHome();
        std::fprintf(stdout, "%s\n%s\n", qPrintable(clinic.homeLine()), qPrintable(clinic.persistLine()));
        return clinic.persistLine().startsWith(QLatin1String("persist failed")) ? 2 : 0;
    }

    QGuiApplication app(argc, argv);
    setAppIdentity(app);
    app.setDesktopFileName(QStringLiteral("org.tiltback.TiltBack"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/org.tiltback.TiltBack.png")));

    const QStringList args = app.arguments();
    const int applyPictureAt = args.indexOf(QStringLiteral("--apply-picture"));
    if (applyPictureAt >= 0) {
        const QString t = applyPictureAt + 1 < args.size() ? args.at(applyPictureAt + 1)
                                                           : QStringLiteral("left");
        ClinicModel clinic;
        clinic.applyPicture(t);
        if (!clinic.pendingPictureRevert()) {
            std::fprintf(stderr, "%s\n%s\n", qPrintable(clinic.pictureError()),
                         qPrintable(clinic.reportText()));
            return 2;
        }
        std::fprintf(stdout, "applied %s — auto-revert in 10s\n", qPrintable(t));
        QTimer::singleShot(11000, &app, &QCoreApplication::quit);
        const int rc = app.exec();
        clinic.refresh();
        std::fprintf(stdout, "%s\n%s\n", qPrintable(clinic.pictureValue()),
                     qPrintable(clinic.reportText()));
        return rc;
    }
    const int applyFingerAt = args.indexOf(QStringLiteral("--apply-finger"));
    if (applyFingerAt >= 0) {
        bool ok = false;
        const int r = applyFingerAt + 1 < args.size() ? args.at(applyFingerAt + 1).toInt(&ok) : 0;
        if (!ok) {
            std::fprintf(stderr, "usage: tiltback --apply-finger 0|1|2|4|8\n");
            return 2;
        }
        ClinicModel clinic;
        clinic.applyFinger(r);
        if (!clinic.pendingFingerRevert()) {
            std::fprintf(stderr, "%s\n%s\n", qPrintable(clinic.fingerDetail()),
                         qPrintable(clinic.reportText()));
            return 2;
        }
        std::fprintf(stdout, "applied finger R=%d — auto-revert in 10s\n", r);
        QTimer::singleShot(11000, &app, &QCoreApplication::quit);
        const int rc = app.exec();
        clinic.refresh();
        std::fprintf(stdout, "%s\n%s\n", qPrintable(clinic.fingerValue()),
                     qPrintable(clinic.reportText()));
        return rc;
    }

    if (args.contains(QStringLiteral("--report"))) {
        ClinicModel clinic;
        std::fprintf(stdout, "%s\n", qPrintable(clinic.reportText()));
        return 0;
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("TiltBack", "Main");

    return app.exec();
}
