#include "../src/OverlayManager.h"
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonDocument>
#include <cstdio>
struct LiveMappingSmoke {
    static void cache(MappingService *service, const QString &root) {
        service->m_root = root;
        if (QCoreApplication::arguments().size() == 3)
            service->m_probe = QCoreApplication::arguments().at(2);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2 || argc > 3)
        return 2;
    bool ok = false;
    auto pid = QString::fromLocal8Bit(argv[1]).toUInt(&ok);
    if (!ok || !pid)
        return 2;
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir prefs;
    if (!prefs.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, prefs.path());
    OverlayManager manager;
    QTemporaryDir cache(QCoreApplication::applicationDirPath() + "/live-mapping-XXXXXX");
    if (!cache.isValid())
        return 2;
    cache.setAutoRemove(false);
    std::printf("LIVE_CACHE %s\n", cache.path().toUtf8().constData());
    LiveMappingSmoke::cache(manager.mappingService(), cache.path());
    bool finished = false, passed = false;
    QObject::connect(manager.mappingService(), &MappingService::eventReceived,
                     [&](const QJsonObject &event) {
                         auto text = QJsonDocument(event).toJson(QJsonDocument::Compact);
                         std::printf("%s\n", text.constData());
                         std::fflush(stdout);
                     });
    auto stop = [&](bool success) {
        if (finished)
            return;
        finished = true;
        passed = success;
        std::printf(
            "LIVE_RESULT success=%d profile=%s mappingState=%s renderer=%d error=%s detail=%s\n",
            success, manager.mappingProfile().toUtf8().constData(),
            manager.mappingState().toUtf8().constData(), manager.rendererActive(),
            manager.errorCode().toUtf8().constData(), manager.errorDetail().toUtf8().constData());
        std::fflush(stdout);
        manager.detach();
        QTimer::singleShot(4000, &app, [&] { app.exit(passed ? 0 : 1); });
    };
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (manager.state() == OverlayManager::State::Error)
            stop(false);
        else if (manager.rendererActive() && !manager.mappingProfile().isEmpty() &&
                 manager.mappingState() == "ready")
            stop(true);
    });
    poll.start();
    QObject::connect(&manager, &OverlayManager::statusMessageChanged, [&] {
        std::printf("STATUS %s\n", manager.statusMessage().toUtf8().constData());
        std::fflush(stdout);
    });
    QTimer::singleShot(180000, &app, [&] { stop(false); });
    QTimer::singleShot(0, &app, [&] {
        if (!manager.attachToProcess(pid))
            stop(false);
    });
    return app.exec();
}
