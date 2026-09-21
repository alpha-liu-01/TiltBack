#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TiltBack"));
    app.setOrganizationDomain(QStringLiteral("tiltback.org"));
    app.setApplicationName(QStringLiteral("TiltBack"));
    app.setDesktopFileName(QStringLiteral("org.tiltback.TiltBack"));

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
