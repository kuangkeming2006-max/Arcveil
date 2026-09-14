#include "OverlayManager.h"
#include "StartupLoader.h"
#include "ApplicationLifecycle.h"
#ifdef MC_OVERLAY_UI_TESTS
#include "../tests/ControllerUiSmoke.h"
#endif
#include <QQmlContext>
#include "ProcessScanner.h"
#include "HypixelApiClient.h"
#include "PlayerStatsService.h"
#include "ApiKeyStore.h"
#include "AppSettings.h"
#include "SkinProfileService.h"
#include "SkinCuboidGeometry.h"
#include "HotkeyCaptureService.h"
#include "BlacklistService.h"
#include "MediaSessionService.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QQmlError>
#include <QDebug>
#include <QTimer>
#include <QUrl>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtQml/qqml.h>

#include <cstdlib>
#include <memory>

namespace {
struct ControllerServices {
    ProcessScanner processScanner;
    OverlayManager overlayManager;
    ApiKeyStore apiKeys;
    AppSettings appSettings;
    HypixelApiClient hypixelApi{&apiKeys};
    PlayerStatsService playerStatsService{&apiKeys};
    SkinProfileService skinProfile;
    HotkeyCaptureService hotkeyCapture;
    BlacklistService blacklistService;
    MediaSessionService mediaSession;
    static void configure(ControllerServices& services)
    {
        auto& processScanner = services.processScanner;
        auto& overlayManager = services.overlayManager;
        auto& apiKeys = services.apiKeys;
        auto& appSettings = services.appSettings;
        auto& hypixelApi = services.hypixelApi;
        auto& playerStatsService = services.playerStatsService;
        auto& skinProfile = services.skinProfile;
        auto& hotkeyCapture = services.hotkeyCapture;
        auto& blacklistService = services.blacklistService;
        auto& mediaSession = services.mediaSession;
        // Restore controller preferences before QML or the native Agent observes
        // them. Changes coming back from the in-game GUI are persisted through the
        // same object, so both interfaces always converge on one value.
        overlayManager.setMenuHotkey(appSettings.menuHotkey());
        overlayManager.setGuiScaleIndex(appSettings.guiScaleIndex());
        QObject::connect(&overlayManager, &OverlayManager::menuHotkeyChanged,
                         &appSettings, [&overlayManager, &appSettings] {
            appSettings.setMenuHotkey(overlayManager.menuHotkey());
        });
        QObject::connect(&overlayManager, &OverlayManager::guiScaleIndexChanged,
                         &appSettings, [&overlayManager, &appSettings] {
            appSettings.setGuiScaleIndex(overlayManager.guiScaleIndex());
        });

        QObject::connect(&apiKeys, &ApiKeyStore::changed,
                         &hypixelApi, &HypixelApiClient::reloadConfiguration);
        QObject::connect(&apiKeys, &ApiKeyStore::changed,
                         &playerStatsService, &PlayerStatsService::reloadConfiguration);

        QObject::connect(&overlayManager, &OverlayManager::hypixelQueryRequested,
                         &hypixelApi, &HypixelApiClient::lookupPlayer);
        QObject::connect(&overlayManager, &OverlayManager::playerFound,
                         &playerStatsService, &PlayerStatsService::enqueuePlayer);
        QObject::connect(&overlayManager, &OverlayManager::playerIdentityFound,
                         &blacklistService, &BlacklistService::observePlayer);
        QObject::connect(&overlayManager, &OverlayManager::agentSessionReady,
                         &blacklistService, &BlacklistService::synchronizeAgent);
        QObject::connect(&blacklistService, &BlacklistService::commandReady,
                         &overlayManager, &OverlayManager::sendBlacklistCommand);
        QObject::connect(&overlayManager, &OverlayManager::blacklistAddRequested,
                         &blacklistService, &BlacklistService::handleAgentAdd);
        QObject::connect(&overlayManager, &OverlayManager::blacklistRemoveRequested,
                         &blacklistService, &BlacklistService::handleAgentRemove);
        QObject::connect(&overlayManager, &OverlayManager::blacklistWarningRequested,
                         &blacklistService, &BlacklistService::handleAgentWarning);
        QObject::connect(&overlayManager, &OverlayManager::blacklistLayoutChanged,
                         &blacklistService, &BlacklistService::handleAgentLayout);
        QObject::connect(&overlayManager, &OverlayManager::blacklistSettingsChanged,
                         &blacklistService, &BlacklistService::handleAgentSettings);
        QObject::connect(&overlayManager, &OverlayManager::matchStateChanged,
                         &playerStatsService, &PlayerStatsService::setMatchActive);
        QObject::connect(&overlayManager, &OverlayManager::playerStatusChanged,
                         &overlayManager, [&overlayManager, &skinProfile] {
            skinProfile.lookup(overlayManager.playerName());
        });
        QObject::connect(&playerStatsService, &PlayerStatsService::statsReady,
                         &overlayManager, &OverlayManager::publishPlayerStats);
        QObject::connect(&playerStatsService, &PlayerStatsService::statsFailed,
                         &overlayManager, &OverlayManager::publishPlayerStatsError);
        const auto publishHypixelToAgent = [&overlayManager, &hypixelApi] {
            const QString status = hypixelApi.errorMessage().isEmpty()
                ? hypixelApi.statusMessage() : hypixelApi.errorMessage();
            overlayManager.publishHypixelResult(
                static_cast<int>(hypixelApi.state()), hypixelApi.queriedUuid(),
                hypixelApi.displayName(), hypixelApi.wins(), hypixelApi.losses(),
                hypixelApi.finalKills(), hypixelApi.finalDeaths(),
                hypixelApi.bedsBroken(), hypixelApi.bedsLost(),
                hypixelApi.winRate(), hypixelApi.fkdr(), status);
        };
        QObject::connect(&hypixelApi, &HypixelApiClient::stateChanged,
                         &overlayManager, publishHypixelToAgent);
        QObject::connect(&hypixelApi, &HypixelApiClient::statsChanged,
                         &overlayManager, publishHypixelToAgent);
        QObject::connect(&hypixelApi, &HypixelApiClient::statusMessageChanged,
                         &overlayManager, publishHypixelToAgent);
        QObject::connect(&hypixelApi, &HypixelApiClient::errorMessageChanged,
                         &overlayManager, publishHypixelToAgent);
        QObject::connect(&overlayManager, &OverlayManager::agentSessionReady,
                         &overlayManager, publishHypixelToAgent);
        QObject::connect(&mediaSession, &MediaSessionService::stateChanged,
                         &overlayManager, &OverlayManager::publishMediaState);
        QObject::connect(&mediaSession, &MediaSessionService::spectrumChanged,
                         &overlayManager, &OverlayManager::publishMediaSpectrum);
        QObject::connect(&overlayManager, &OverlayManager::agentSessionReady,
                         &mediaSession, &MediaSessionService::republish);
        QObject::connect(&overlayManager, &OverlayManager::mediaPreviousRequested,
                         &mediaSession, &MediaSessionService::previous);
        QObject::connect(&overlayManager, &OverlayManager::mediaNextRequested,
                         &mediaSession, &MediaSessionService::next);
        QObject::connect(&overlayManager, &OverlayManager::mediaToggleRequested,
                         &mediaSession, &MediaSessionService::togglePlayback);

        QObject::connect(&overlayManager, &OverlayManager::targetExited,
                         &processScanner, [&processScanner](quint32) {
            processScanner.selectProcess(0);
            processScanner.refresh();
        });

        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "ProcessScanner", &processScanner);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "OverlayManager", &overlayManager);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "HotkeyCapture", &hotkeyCapture);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "HypixelApi", &hypixelApi);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "ApiKeys", &apiKeys);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "AppSettings", &appSettings);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "SkinProfile", &skinProfile);
        qmlRegisterSingletonInstance("McOverlay", 1, 0,
                                     "Blacklist", &blacklistService);
        qmlRegisterType<SkinCuboidGeometry>("McOverlay", 1, 0, "SkinCuboidGeometry");


    }
};
} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("MinecraftOverlayManager"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Java Overlay Studio"));
    QGuiApplication::setOrganizationName(QStringLiteral("Overlay Studio"));

    // Tests require an explicit command-line option. An inherited environment
    // variable (even one containing "0") must never auto-exit a user's session.
    const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
    // Smoke tests must never read API keys or overwrite the user's preferences.
    std::unique_ptr<QTemporaryDir> testPreferences;
    if (smokeTest) {
        QStandardPaths::setTestModeEnabled(true);
        testPreferences = std::make_unique<QTemporaryDir>();
        if (!testPreferences->isValid()) return EXIT_FAILURE;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testPreferences->path());
    }
    ApplicationLifecycle lifecycle;

    // The controller UI remains hardware accelerated. The Minecraft overlay is
    // no longer a second Qt window: McOverlayAgent.dll renders Dear ImGui inside
    // the target process' active OpenGL context.
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(smokeTest && application.arguments().contains(QStringLiteral("--software-renderer"))
        ? QSGRendererInterface::Software
        : QSGRendererInterface::Direct3D11);
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Singletons outlive the QML engine; construction waits for the splash's
    // first frame. No nested event loops or blocking processEvents() calls.
    std::unique_ptr<ControllerServices> services;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Lifecycle"), &lifecycle);
    bool qmlErrors = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     &application, [&qmlErrors](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) qCritical().noquote() << warning.toString();
        qmlErrors = true;
    });
    StartupLoader loader(engine, [&] {
        services = std::make_unique<ControllerServices>();
        ControllerServices::configure(*services);
    }, [&] {
        // Defer scanning until the application itself can paint and respond.
        if (!smokeTest) QTimer::singleShot(0, &services->processScanner, &ProcessScanner::refresh);
        if (smokeTest) {
#ifdef MC_OVERLAY_UI_TESTS
            new ControllerUiSmoke(loader.mainWindow(), [&](bool ok) {
            if (!ok || qmlErrors) QCoreApplication::exit(EXIT_FAILURE);
            else lifecycle.requestExit(QStringLiteral("successful controller smoke test"));
            });
#else
            QTimer::singleShot(1000, &application, [&] {
                if (qmlErrors) QCoreApplication::exit(EXIT_FAILURE);
                else lifecycle.requestExit(QStringLiteral("successful startup smoke test"));
            });
#endif
        }
    });
    QTimer::singleShot(0, &loader, &StartupLoader::start);
    // A broken asynchronous load must fail CI rather than hang forever.
    if (smokeTest) QTimer::singleShot(20000, &application, [] {
        qCritical() << "Startup smoke test timed out";
        QCoreApplication::exit(EXIT_FAILURE);
    });
    return application.exec();
}
