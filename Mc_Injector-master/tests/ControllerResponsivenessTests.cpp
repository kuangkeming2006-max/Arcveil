#include "../src/OverlayManager.h"
#include "../src/ProcessScanner.h"
#include <QCoreApplication>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QEventLoop>
#include <cstdio>

struct ControllerResponsivenessTests {
    static bool run() {
        // Renderer readiness alone must never authorize a verified mapping.
        {
            OverlayManager guarded;
            auto owner = std::make_shared<AttachTransaction>();
            owner->pid = quint32(QCoreApplication::applicationPid());
            owner->processStart = MappingService::processStartFor(owner->pid);
            owner->state = AttachTransaction::State::LoadingAgent;
            guarded.m_transaction = owner; guarded.m_targetPid = owner->pid;
            guarded.m_agentTransactionId = owner->transactionId;
            guarded.m_authenticated = true;
            guarded.setState(OverlayManager::State::WaitingForAgent);
            guarded.processAgentLine("RENDERER_READY OpenGL");
            if (guarded.state() == OverlayManager::State::Active || guarded.statusMessage().contains("is active")) {
                std::puts("FAIL renderer authorized an unbound mapping"); return false;
            }
            guarded.processAgentLine("BINDING_READY 15 120 300");
            if (guarded.state() != OverlayManager::State::Active) {
                std::puts("FAIL exact binding plus renderer did not become Active"); return false;
            }
            guarded.m_runtimeBindingsReady = false;
            guarded.setState(OverlayManager::State::WaitingForAgent);
            guarded.processAgentLine("BINDING_READY 0 0 0");
            if (guarded.state() != OverlayManager::State::Error) {
                std::puts("FAIL malformed binding receipt accepted"); return false;
            }
        }
        OverlayManager manager;
        manager.m_transaction = std::make_shared<AttachTransaction>();
        const QByteArray attachDisabled =
            "Exception in thread \"main\" java.lang.InternalError: "
            "Remote thread failed for unknown reason (100)";
        if (!OverlayManager::isRecoverableJvmAttachFailure(1, attachDisabled)
            || !OverlayManager::isRecoverableJvmAttachFailure(10, {})
            || !OverlayManager::isRecoverableJvmAttachFailure(12, {})
            || !OverlayManager::isRecoverableJvmAttachFailure(13, {})
            || OverlayManager::isRecoverableJvmAttachFailure(
                1, "Remote thread failed for unknown reason (103)")) {
            std::puts("FAIL Windows Attach recovery classification");
            return false;
        }
        // F: old MappingService result cannot load an Agent for a replacement owner.
        manager.m_transaction->pid = 111;
        manager.m_targetPid = 111;
        manager.m_transaction->state = AttachTransaction::State::MappingVerified;
        manager.setState(OverlayManager::State::Validating);
        emit manager.m_mappingService.readyForTransaction("old-transaction", "ignored", "ignored");
        if (manager.state() != OverlayManager::State::Validating || manager.m_server.isListening() ||
            manager.m_attachProcess.state() != QProcess::NotRunning) {
            std::puts("FAIL F: late ready started Agent"); return false;
        }
        manager.m_transaction->invalidate();
        emit manager.m_mappingService.readyForTransaction(manager.transactionId(), "ignored", "ignored");
        if (manager.m_server.isListening()) { std::puts("FAIL E: cancelled owner started Agent"); return false; }
        manager.m_transaction = std::make_shared<AttachTransaction>();
        manager.m_targetPid = 0;
        manager.setState(OverlayManager::State::Detached);
        // I: browsing selection has no session capability and cannot detach Active.
        {
            ProcessScanner browser;
            browser.m_processes = {{111, "javaw.exe", {}, "A", 0, 0}, {222, "javaw.exe", {}, "B", 0, 0}};
            manager.m_transaction->pid = 111;
            manager.m_transaction->state = AttachTransaction::State::Active;
            manager.m_targetPid = 111;
            manager.setState(OverlayManager::State::Active);
            const auto id = manager.transactionId();
            browser.selectProcess(111); browser.selectProcess(222); browser.selectProcess(0);
            if (!manager.attached() || manager.targetPid() != 111 || manager.transactionId() != id) {
                std::puts("FAIL I: scanner selection changed active session"); return false;
            }
            manager.m_targetPid = 0;
            manager.setState(OverlayManager::State::Detached);
        }
        manager.m_agentSocket = new QLocalSocket(&manager);
        manager.m_authenticated = true;
        int delivered = 0;
        QObject::connect(&manager, &OverlayManager::interactiveChanged, [&] { ++delivered; });
        for (int i=0; i<2000; ++i)
            manager.m_agentReadBuffer += (i%2 == 0 ? "STATE_CHANGED 1 1\n" : "STATE_CHANGED 1 0\n");
        bool heartbeat = false;
        QTimer::singleShot(0, &manager, [&] { heartbeat = true; });
        manager.readAgentMessages();
        if (delivered <= 0 || delivered > 64 || manager.m_agentReadBuffer.isEmpty()) return false;
        QElapsedTimer clock; clock.start();
        while (delivered < 2000 && clock.elapsed() < 4000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (!heartbeat || delivered != 2000 || !manager.m_agentReadBuffer.isEmpty()) return false;
        // A split line must survive a turn without a newline, then complete once.
        manager.m_agentReadBuffer = "STATE_CHANGED 1 ";
        manager.readAgentMessages();
        if (manager.m_agentReadBuffer.isEmpty()) return false;
        manager.m_agentReadBuffer += "1\n";
        manager.readAgentMessages();
        if (delivered != 2001) return false;
        int previousActions = 0, nextActions = 0, toggleActions = 0;
        QObject::connect(&manager, &OverlayManager::mediaPreviousRequested,
                         [&] { ++previousActions; });
        QObject::connect(&manager, &OverlayManager::mediaNextRequested,
                         [&] { ++nextActions; });
        QObject::connect(&manager, &OverlayManager::mediaToggleRequested,
                         [&] { ++toggleActions; });
        manager.processAgentLine("MEDIA_ACTION PREVIOUS");
        manager.processAgentLine("MEDIA_ACTION NEXT");
        manager.processAgentLine("MEDIA_ACTION TOGGLE");
        manager.processAgentLine("MEDIA_ACTION NONE");
        if (previousActions != 1 || nextActions != 1 || toggleActions != 1) {
            std::puts("FAIL media action dispatch"); return false;
        }
        manager.processAgentLine(
            "MEDIA_SETTINGS_CHANGED 1 70 79 35 34 1118481 -1 -1 88 2");
        {
            QSettings mediaSettings;
            mediaSettings.beginGroup(QStringLiteral("MediaOverlay"));
            const int migratedScale = mediaSettings.value(
                QStringLiteral("scalePercent"), -1).toInt();
            mediaSettings.endGroup();
            if (migratedScale != 68) {
                std::puts("FAIL legacy media scale migration"); return false;
            }
        }
        int blacklistScale = 0;
        QString blacklistColor;
        QObject::connect(&manager, &OverlayManager::blacklistSettingsChanged,
            [&](bool, bool, bool, bool, bool, int, int scale,
                const QString &color) {
                blacklistScale = scale;
                blacklistColor = color;
            });
        manager.processAgentLine(
            "BLACKLIST_SETTINGS_CHANGED 1 1 1 1 0 82 145 1122867");
        if (blacklistScale != 145 || blacklistColor != QStringLiteral("#112233")) {
            std::puts("FAIL blacklist content scale protocol"); return false;
        }
        const QByteArray payload = " 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0 1 1 0 0 0 0 1 0 1 1 0 10 32 164 78 9 76 100 -1 -1 0 16777215 0 1644065 0 100 88 1052946 8543720 1 48 1 8543720 100 120 55 -5 119 100 45 35 8316927 -1 -1 100 0 0 0 1 0 100 16751933 0 16 90 100 100 96 67 2 4 500 100";
        manager.processAgentLine("FEATURE_STATE_CHANGED_V2" + payload);
        if (!manager.aimLockOnMode()) return false;
        QList<QByteArray> v3Fields=payload.trimmed().split(' ');
        v3Fields.insert(68,QByteArray::number(0x1A400U));
        manager.processAgentLine("FEATURE_STATE_CHANGED_V3 "+v3Fields.join(' '));
        if(manager.m_featureHotkeysPackedC!=0x1A400U ||
           !manager.textGuiModules().contains(QStringLiteral("FreeLook"))) {
            std::puts("FAIL FreeLook v3 feature protocol"); return false;
        }
        // Separate optional flags cannot reinterpret the old packed protocol.
        if (manager.aimSilentLock() || !manager.aimScannerEnabled()) { std::puts("FAIL optional aim defaults"); return false; }
        manager.processAgentLine("AIM_OPTIONS_CHANGED 7");
        if (!manager.aimSilentLock() || !manager.aimScannerEnabled() || !manager.bedBreakerEnabled()) {
            std::puts("FAIL optional aim command"); return false;
        }
        // v42 assigns bit 30 to the independent Silent Control adaptation.
        manager.processAgentLine("AIM_OPTIONS_CHANGED 2013265927");
        if(!manager.m_silentFileDebug || !manager.m_silentChatDebug ||
           !manager.m_aimSequentialTargets || !manager.m_silentControlAdaptation) return false;
        manager.processAgentLine("AIM_ATTACK_CPS_CHANGED 17");
        manager.processAgentLine("AIM_ATTACK_CPS_CHANGED 0");
        manager.processAgentLine("AIM_ATTACK_CPS_CHANGED 21");
        manager.processAgentLine("AIM_ATTACK_CPS_CHANGED 9 extra");
        if(manager.aimAttackCps()!=17) {
            std::puts("FAIL fixed CPS protocol validation"); return false;
        }
        for (const char* invalid : {"AIM_OPTIONS_CHANGED -1", "AIM_OPTIONS_CHANGED 2147483648",
             "AIM_OPTIONS_CHANGED 2 extra", "AIM_OPTIONS_CHANGED nope"})
            manager.processAgentLine(invalid);
        if (!manager.aimSilentLock() || !manager.aimScannerEnabled() || !manager.bedBreakerEnabled()) {
            std::puts("FAIL optional aim invalid-input guard"); return false;
        }
        manager.setAimScannerEnabled(false);
        // Production intentionally debounces disk writes off the input path.
        QElapsedTimer persistenceClock; persistenceClock.start();
        while(manager.m_featureSettingsStoreTimer.isActive() && persistenceClock.elapsed()<1500)
            QCoreApplication::processEvents(QEventLoop::AllEvents,5);
        if(manager.m_featureSettingsStoreTimer.isActive()) return false;
        {
            OverlayManager restored;
            if (!restored.aimSilentLock() || restored.aimScannerEnabled() ||
                !restored.m_silentFileDebug || !restored.m_silentChatDebug ||
                !restored.m_silentControlAdaptation||restored.aimAttackCps()!=17) {
                std::puts("FAIL optional aim persistence"); return false; }
        }
        manager.setMenuHotkey(119);
        manager.setGuiScaleIndex(3);
        manager.setGuiFontSize(21);
        manager.setGuiFontWeight(700);
        manager.setGuiElementScale(125);
        manager.setClickGuiWidthPercent(40);
        manager.setClickGuiHeightPercent(40);
        while(manager.m_featureSettingsStoreTimer.isActive() && persistenceClock.elapsed()<3000)
            QCoreApplication::processEvents(QEventLoop::AllEvents,5);
        if(manager.m_featureSettingsStoreTimer.isActive() ||
           !manager.saveConfig(QStringLiteral("Input profile"))) {
            std::puts("FAIL input config save"); return false;
        }
        manager.setMenuHotkey(120);
        manager.setGuiScaleIndex(0);
        manager.setGuiFontSize(14);
        manager.setGuiFontWeight(400);
        manager.setGuiElementScale(60);
        manager.setClickGuiWidthPercent(100);
        manager.setClickGuiHeightPercent(100);
        if(!manager.applyConfig(QStringLiteral("Input profile")) ||
           manager.menuHotkey()!=119 || manager.guiScaleIndex()!=3 ||
           manager.guiFontSize()!=21 || manager.guiFontWeight()!=700 ||
           manager.guiElementScale()!=125 || manager.clickGuiWidthPercent()!=40 ||
           manager.clickGuiHeightPercent()!=40) {
            std::puts("FAIL input config restore"); return false;
        }
        manager.processAgentLine("GUI_TYPOGRAPHY_CHANGED 25 600");
        manager.processAgentLine("GUI_TYPOGRAPHY_CHANGED 18 500");
        manager.processAgentLine("GUI_TYPOGRAPHY_CHANGED 18 400 extra");
        if(manager.guiFontSize()!=21 || manager.guiFontWeight()!=700) {
            std::puts("FAIL invalid typography accepted");return false;
        }
        manager.processAgentLine("GUI_TYPOGRAPHY_CHANGED 20 600");
        if(manager.guiFontSize()!=20 || manager.guiFontWeight()!=600) {
            std::puts("FAIL native typography echo");return false;
        }
        int elementNotifications=0;
        QObject::connect(&manager,&OverlayManager::guiElementScaleChanged,[&]{++elementNotifications;});
        for(const char* invalid:{"GUI_ELEMENT_SCALE_CHANGED 59","GUI_ELEMENT_SCALE_CHANGED 151",
            "GUI_ELEMENT_SCALE_CHANGED nope","GUI_ELEMENT_SCALE_CHANGED 120 extra"})
            manager.processAgentLine(invalid);
        if(manager.guiElementScale()!=125 || elementNotifications!=0) {
            std::puts("FAIL invalid element scale accepted");return false;
        }
        manager.processAgentLine("GUI_ELEMENT_SCALE_CHANGED 120");
        manager.processAgentLine("GUI_ELEMENT_SCALE_CHANGED 120");
        if(manager.guiElementScale()!=120 || manager.guiScaleIndex()!=3 ||
            manager.guiFontSize()!=20 || elementNotifications!=1) {
            std::puts("FAIL independent element scale echo");return false;
        }
        manager.setGuiElementScale(0);
        if(manager.guiElementScale()!=60) return false;
        manager.setGuiElementScale(999);
        if(manager.guiElementScale()!=150) return false;
        manager.setGuiElementScale(120);
        manager.flushFeatureSettings();
        {
            OverlayManager restored;
            if(restored.guiElementScale()!=120 || restored.clickGuiWidthPercent()!=40 ||
                restored.clickGuiHeightPercent()!=40) {
                std::puts("FAIL GUI layout persistence");return false;
            }
        }
        // V3 payload positions are unchanged: 78/79 include the message token.
        auto layoutFields=v3Fields;
        layoutFields[77]="40";layoutFields[78]="40";
        manager.processAgentLine("FEATURE_STATE_CHANGED_V3 "+layoutFields.join(' '));
        if(manager.clickGuiWidthPercent()!=40 || manager.clickGuiHeightPercent()!=40) {
            std::puts("FAIL 40 percent layout echo");return false;
        }
        layoutFields[77]="39";layoutFields[78]="100";
        manager.processAgentLine("FEATURE_STATE_CHANGED_V3 "+layoutFields.join(' '));
        if(manager.clickGuiWidthPercent()!=40 || manager.clickGuiHeightPercent()!=40) {
            std::puts("FAIL invalid layout echo accepted");return false;
        }
        // Optional statistics failures may update status, but must keep the
        // authenticated transport and resident Agent session alive.
        manager.processAgentLine("ERROR BAD_HYPIXEL_RESULT invalid-payload");
        if(!manager.m_authenticated || !manager.m_agentSocket ||
           manager.errorCode()==QStringLiteral("BAD_HYPIXEL_RESULT")) {
            std::puts("FAIL Hypixel error detached session"); return false;
        }
        manager.setAimSilentLock(false);
        manager.setAimScannerEnabled(true);
        manager.processAgentLine("FEATURE_STATE_CHANGED" + payload);
        if (manager.aimLockOnMode()) return false; // Old slowdown is never Lock On.
        manager.processAgentLine("FEATURE_STATE_CHANGED_V2" + payload);
        if (!manager.aimLockOnMode()) return false;
        // A stale continuation after transport teardown is harmless.
        manager.m_authenticated = false;
        manager.m_agentReadBuffer.clear();
        delete manager.m_agentSocket; manager.m_agentSocket = nullptr;
        manager.readAgentMessages();
        return true;
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir preferences;
    if (!preferences.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferences.path());
    QCoreApplication::setOrganizationName("OverlayRegression");
    QCoreApplication::setApplicationName("Responsiveness");
    if (!ControllerResponsivenessTests::run()) { std::puts("FAIL IPC bounded delivery"); return 1; }
    ProcessScanner scanner;
    scanner.refreshOnce();
    if (!scanner.refreshing()) return 1; // Completion must be posted, never inline.
    bool heartbeat=false;
    QTimer::singleShot(0,&scanner,[&] { heartbeat=true; });
    QElapsedTimer clock; clock.start();
    while (scanner.refreshing() && clock.elapsed()<5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents,5);
    if (!heartbeat || scanner.refreshing()) { std::puts("FAIL async scanner"); return 1; }
    int polls=0, heartbeats=0;
    QObject::connect(&scanner,&ProcessScanner::lastRefreshChanged,[&] { ++polls; });
    QTimer pulse; pulse.setInterval(20);
    QObject::connect(&pulse,&QTimer::timeout,[&] { ++heartbeats; }); pulse.start();
    scanner.refresh(); clock.restart();
    while (scanner.refreshing() && clock.elapsed()<8000)
        QCoreApplication::processEvents(QEventLoop::AllEvents,5);
    if(scanner.refreshing() || clock.elapsed()<5000 || polls<5 || heartbeats<100 ||
       scanner.scanProgress()<0.99) { std::puts("FAIL five-second progressive scan"); return 1; }
    std::printf("Five-second scan: %lld ms, %d polls, %d UI heartbeats\n",clock.elapsed(),polls,heartbeats);
    std::puts("Controller responsiveness: bounded IPC, split lines, versioned mode, teardown and async scan passed.");
    return 0;
}
