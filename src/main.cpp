#include "clinicmodel.h"
#include "followengine.h"
#include "kwininput.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
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

    if (app.arguments().contains(QStringLiteral("--report"))) {
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
