#include "clinicmodel.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <cstdio>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TiltBack"));
    app.setOrganizationDomain(QStringLiteral("tiltback.org"));
    app.setApplicationName(QStringLiteral("TiltBack"));
    app.setDesktopFileName(QStringLiteral("org.tiltback.TiltBack"));

    const QStringList args = app.arguments();
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
