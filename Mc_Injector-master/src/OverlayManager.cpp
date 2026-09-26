#include "OverlayManager.h"
#include "OverlayManagerCodec.internal.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <tlhelp32.h>
#endif


using namespace overlay_detail;

namespace {
constexpr int kAttachTimeoutMilliseconds = 15000;
constexpr int kDetachTimeoutMilliseconds = 2500;
}

OverlayManager::OverlayManager(QObject *parent)
    : QObject(parent)
{
    loadFeatureSettings();
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("Config"));
        m_configAutoSave = settings.value(QStringLiteral("autoSave"), false).toBool();
        m_configNames = settings.value(QStringLiteral("names")).toStringList();
        m_activeConfig = settings.value(QStringLiteral("active")).toString();
        settings.endGroup();
        m_configNames.removeDuplicates();
        m_configNames.erase(std::remove_if(m_configNames.begin(), m_configNames.end(),
            [](const QString &name) { return !validConfigName(name); }),
            m_configNames.end());
        if (!m_configNames.contains(m_activeConfig)) m_activeConfig.clear();
    }
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection,
            this, &OverlayManager::acceptAgentConnection);

    m_attachProcess.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_attachProcess, &QProcess::readyReadStandardOutput, this, [this] {
        m_helperStandardOutput += m_attachProcess.readAllStandardOutput();
    });
    connect(&m_attachProcess, &QProcess::readyReadStandardError, this, [this] {
        m_helperStandardError += m_attachProcess.readAllStandardError();
    });
    connect(&m_attachProcess, &QProcess::finished,
            this, &OverlayManager::handleAttachFinished);
    connect(&m_attachProcess, &QProcess::errorOccurred,
            this, &OverlayManager::handleAttachError);

    m_attachTimeout.setSingleShot(true);
    m_attachTimeout.setInterval(kAttachTimeoutMilliseconds);
    connect(&m_attachTimeout, &QTimer::timeout, this, [this] {
        if (!m_authenticated) {
            fail(QStringLiteral("AGENT_HANDSHAKE_TIMEOUT"),
                 QStringLiteral("No authenticated native-agent connection arrived within 15 seconds."));
        }
    });

    m_nativeFallbackGrace.setSingleShot(true);
    m_nativeFallbackGrace.setInterval(2200);
    connect(&m_nativeFallbackGrace, &QTimer::timeout, this, [this] {
        if (!m_authenticated && m_state == State::WaitingForAgent
            && m_loaderKind == LoaderKind::JvmAttach
            && !m_nativeFallbackAttempted) {
            (void) startNativeLoaderFallback();
        }
    });

    // A graceful detach is a protocol transaction. The agent first drains
    // render callbacks and restores its hooks, then replies DETACH_COMPLETE.
    // Only an unresponsive peer reaches this short force-close deadline.
    m_detachTimeout.setSingleShot(true);
    m_detachTimeout.setInterval(kDetachTimeoutMilliseconds);
    connect(&m_detachTimeout, &QTimer::timeout, this, [this] {
        if (m_state == State::Detaching && !m_detachTransportComplete)
            completeDetach(true);
    });

    m_targetMonitor.setInterval(1000);
    connect(&m_targetMonitor, &QTimer::timeout,
            this, &OverlayManager::monitorTarget);

    // Telemetry is rate-limited by the agent (currently at most 10 Hz). A
    // coarse controller timer marks the last snapshot stale without trusting
    // the JVM's clock or waking the Qt event loop for every freshness check.
    m_gameStateFreshnessTimer.setInterval(1000);
    connect(&m_gameStateFreshnessTimer, &QTimer::timeout,
            this, &OverlayManager::refreshGameStateFreshness);
    m_gameStateReceiptClock.start();
    m_gameStateFreshnessTimer.start();

    // Color pickers can update once per rendered frame. Coalesce those
    // changes into one registry write after interaction settles so the
    // controller never turns an in-game drag into synchronous I/O churn.
    m_featureSettingsStoreTimer.setSingleShot(true);
    m_featureSettingsStoreTimer.setInterval(300);
    connect(&m_featureSettingsStoreTimer, &QTimer::timeout,
            this, &OverlayManager::flushFeatureSettings);
}

OverlayManager::~OverlayManager()
{
    // QObject destruction cannot depend on another event-loop turn. Make one
    // best-effort protocol write, then release local resources without any
    // waitForFinished/waitForStarted call on the GUI thread.
    m_destroying = true;
    m_pendingAttachPid = 0;
    m_attachTimeout.stop();
    m_nativeFallbackGrace.stop();
    m_detachTimeout.stop();
    m_targetMonitor.stop();
    m_featureSettingsStoreTimer.stop();
    flushFeatureSettings();
    if (m_authenticated) {
        writeAgentCommand(QByteArrayLiteral("DETACH\n"));
        if (m_agentSocket)
            m_agentSocket->flush();
    }
    disconnect(&m_attachProcess, nullptr, this, nullptr);
    (void) closeSessionTransport();
}

bool OverlayManager::attached() const noexcept
{
    return m_state == State::WaitingForOpenGL || m_state == State::Active;
}

bool OverlayManager::busy() const noexcept
{
    return m_state == State::Validating
        || m_state == State::StartingIpc
        || m_state == State::LaunchingAttachHelper
        || m_state == State::WaitingForAgent
        || m_state == State::Detaching;
}

bool OverlayManager::attachToProcess(quint32 pid)
{
    if (pid == 0) {
        fail(QStringLiteral("INVALID_PID"),
             QStringLiteral("Select a Java process before loading the native agent."));
        return false;
    }

    // Loading another process while a session is live is a queued transition:
    // finish the authenticated DETACH transaction first, then start the new
    // helper from a later event-loop turn. This also avoids reusing QProcess
    // while an earlier helper is still emitting its terminal signals.
    if (m_state == State::Detaching) {
        m_pendingAttachPid = pid;
        setStatusMessage(QStringLiteral("Finishing the previous detach before attaching to PID %1...")
                             .arg(pid));
        return true;
    }
    if (m_state != State::Detached || m_targetPid != 0 || m_agentSocket
        || m_server.isListening()
        || m_attachProcess.state() != QProcess::NotRunning) {
        m_pendingAttachPid = pid;
        beginDetach();
        return true;
    }
    (void) closeSessionTransport();
    m_pendingAttachPid = 0;

    clearError();
    setRenderer({});
    resetGameState();
    m_pipeToken.clear();
    m_agentDllPath.clear();
    m_agentOptions.clear();
    m_loaderKind = LoaderKind::None;
    m_nativeFallbackAttempted = false;
    m_nativeFallbackGrace.stop();
    if (m_targetPid != 0 || !m_targetTitle.isEmpty()) {
        m_targetPid = 0;
        m_targetTitle.clear();
        emit targetChanged();
    }
    setState(State::Validating);

    const QString executablePath = targetExecutablePath(pid);
    if (executablePath.isEmpty()) {
        fail(QStringLiteral("PROCESS_ACCESS_DENIED"),
             QStringLiteral("Windows could not query the selected Java process. Run both programs at the same integrity level."));
        return false;
    }
    if (!targetArchitectureSupported(pid)) {
        fail(QStringLiteral("UNSUPPORTED_ARCHITECTURE"),
             QStringLiteral("McOverlayAgent is x64 and can only be loaded into an x64 JVM."));
        return false;
    }
    // A detached native-agent DLL normally remains resident in HotSpot.
    // Agent_OnAttach and McOverlay_Start are intentionally restartable, so an
    // existing module is not itself an error during a fresh IPC session.

    const QString agentDll = locateAgentDll();
    const QString attachHelper = locateAttachHelper();
    const JavaRuntime java = locateJavaRuntime(executablePath);
    if (agentDll.isEmpty()) {
        fail(QStringLiteral("AGENT_NOT_FOUND"),
             QStringLiteral("McOverlayAgent.dll was not found beside the application."));
        return false;
    }
    if (attachHelper.isEmpty()) {
        fail(QStringLiteral("ATTACH_HELPER_NOT_FOUND"),
             QStringLiteral("McOverlayAttachHelper.jar was not found beside the application."));
        return false;
    }
    if (java.executable.isEmpty()) {
        fail(QStringLiteral("ATTACH_JDK_NOT_FOUND"),
             QStringLiteral("A JDK containing the jdk.attach API is required. Configure JAVA_HOME or install a current x64 JDK."));
        return false;
    }

    m_targetPid = pid;
    m_targetTitle = targetWindowTitle(pid).trimmed();
    if (m_targetTitle.isEmpty())
        m_targetTitle = QStringLiteral("Minecraft 1.8.9 (PID %1)").arg(pid);
    emit targetChanged();

    setState(State::StartingIpc);
    m_pipeToken = QUuid::createUuid().toString(QUuid::WithoutBraces)
                      .remove(QLatin1Char('-'));
    const QString serverName = QStringLiteral("McOverlay-%1-%2")
                                   .arg(pid)
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!m_server.listen(serverName)) {
        fail(QStringLiteral("IPC_LISTEN_FAILED"), m_server.errorString());
        return false;
    }

    const QString options = QStringLiteral("pipe=%1;token=%2;protocol=1")
                                .arg(m_server.fullServerName(), m_pipeToken);
    m_agentDllPath = agentDll;
    m_agentOptions = options;
    QStringList arguments;
    if (java.modular) {
        arguments << QStringLiteral("--add-modules") << QStringLiteral("jdk.attach")
                  << QStringLiteral("-jar") << attachHelper;
    } else {
        const QString classPath = attachHelper + QLatin1Char(';') + java.toolsJar;
        arguments << QStringLiteral("-cp") << classPath
                  << QStringLiteral("com.mcoverlay.attach.AttachHelper");
    }
    arguments << QString::number(pid) << agentDll << options;

    m_helperStandardOutput.clear();
    m_helperStandardError.clear();
    m_jvmAttachFallbackReason.clear();
    m_authenticated = false;
    m_loaderKind = LoaderKind::JvmAttach;
    setState(State::LaunchingAttachHelper);
    setStatusMessage(QStringLiteral("Loading the JNI/JVMTI agent into %1...")
                         .arg(m_targetTitle));
    m_attachProcess.setProgram(java.executable);
    m_attachProcess.setArguments(arguments);
    m_attachProcess.start();
    setState(State::WaitingForAgent);
    m_attachTimeout.start();
    m_targetMonitor.start();
    return true;
}

void OverlayManager::detach()
{
    // An explicit user detach cancels a previously queued process switch.
    m_pendingAttachPid = 0;
    beginDetach();
}

void OverlayManager::beginDetach()
{
    if (m_state == State::Detaching)
        return;

    const bool hasTransport = m_agentSocket || m_server.isListening();
    const bool helperRunning = m_attachProcess.state() != QProcess::NotRunning;
    if (m_state == State::Detached && m_targetPid == 0
        && !hasTransport && !helperRunning) {
        return;
    }

    m_attachTimeout.stop();
    m_targetMonitor.stop();
    m_detachTransportComplete = false;
    m_detachTimedOut = false;
    setState(State::Detaching);
    setStatusMessage(QStringLiteral("Waiting for the native agent to restore its OpenGL hooks..."));

    // Only an authenticated peer may receive controller commands. During an
    // incomplete attach there is no runtime whose shutdown can be confirmed,
    // so close that partial session immediately and wait asynchronously for
    // the helper's QProcess::finished signal if necessary.
    if (m_authenticated && m_agentSocket
        && m_agentSocket->state() == QLocalSocket::ConnectedState) {
        writeAgentCommand(QByteArrayLiteral("DETACH\n"));
        m_agentSocket->flush();
        m_detachTimeout.start();
        return;
    }

    completeDetach(false);
}

void OverlayManager::completeDetach(const bool timedOut)
{
    if (m_detachTransportComplete)
        return;

    m_detachTimeout.stop();
    m_detachTimedOut = timedOut;
    m_detachTransportComplete = true;
    const bool helperStopped = closeSessionTransport();

    const bool hadTarget = m_targetPid != 0 || !m_targetTitle.isEmpty();
    m_targetPid = 0;
    m_targetTitle.clear();
    m_pipeToken.clear();
    setRenderer({});
    resetGameState();
    if (hadTarget)
        emit targetChanged();

    if (helperStopped)
        finalizeDetachedState();
    else
        setStatusMessage(QStringLiteral("Native session closed; waiting for the attach helper to exit..."));
}

void OverlayManager::finalizeDetachedState()
{
    if (m_destroying)
        return;

    m_detachTransportComplete = false;
    clearError();
    setState(State::Detached);
    setStatusMessage(m_detachTimedOut
        ? QStringLiteral("Native overlay detached after the agent response timed out")
        : QStringLiteral("Native overlay is detached"));
    m_detachTimedOut = false;
    startPendingAttach();
}

void OverlayManager::startPendingAttach()
{
    if (m_destroying || m_pendingAttachPid == 0
        || m_attachProcess.state() != QProcess::NotRunning) {
        return;
    }

    const quint32 pid = std::exchange(m_pendingAttachPid, 0U);
    QTimer::singleShot(0, this, [this, pid] {
        if (!m_destroying && m_state == State::Detached)
            (void) attachToProcess(pid);
    });
}

void OverlayManager::handleAgentDisconnected()
{
    if (m_agentSocket) {
        m_agentSocket->deleteLater();
        m_agentSocket = nullptr;
    }
    if (m_state == State::Detaching) {
        // A peer close is also a valid detach boundary. Older agents may not
        // emit DETACH_COMPLETE, but their disconnected pipe proves they can no
        // longer receive controller state or retain this IPC session.
        completeDetach(false);
    } else if (m_state != State::Detached && m_state != State::Error &&
               m_loaderKind == LoaderKind::JvmAttach &&
               !m_nativeFallbackAttempted && targetProcessIsRunning(m_targetPid)) {
        // A few Forge 1.8.9 VMs complete asynchronous Agent_OnAttach and then
        // tear down that first control-pipe worker while the launcher is
        // reporting its compatibility result. Keep the authenticated server
        // and token alive: a duplicate bootstrap can reconnect, otherwise the
        // existing bounded grace timer advances to McOverlay_Start exactly
        // once. AgentRuntime::start serializes and cleanly replaces the stopped
        // same-session runtime, so this is recovery rather than double load.
        m_authenticated = false;
        m_jvmAttachFallbackReason = QStringLiteral(
            "The JVM Attach agent disconnected before the session stabilized.");
        setState(State::WaitingForAgent);
        setStatusMessage(QStringLiteral(
            "Forge agent pipe closed during startup; waiting briefly before native recovery..."));
        m_nativeFallbackGrace.start();
    } else if (m_state != State::Detached && m_state != State::Error) {
        fail(QStringLiteral("AGENT_DISCONNECTED"),
             QStringLiteral("The native agent disconnected from its control pipe."));
    }
}

void OverlayManager::handleAttachFinished(int exitCode,
                                          QProcess::ExitStatus exitStatus)
{
    m_helperStandardOutput += m_attachProcess.readAllStandardOutput();
    m_helperStandardError += m_attachProcess.readAllStandardError();

    if (m_state == State::Detaching) {
        if (m_detachTransportComplete)
            finalizeDetachedState();
        return;
    }
    if (m_closingTransport || m_state == State::Detached
        || m_state == State::Error)
        return;
    // The helper process is only a launcher. Once HELLO has authenticated the
    // resident agent, its later exit status must not move an already-live
    // session back to WaitingForAgent or start a second bootstrap path.
    if (m_authenticated)
        return;
    // Some Forge 1.8.9 HotSpot builds surface a native Agent_OnAttach load as
    // AgentLoadException("Failed to load agent library: 0") (helper exit 12),
    // even though the same exact-path DLL is safe to start through its explicit
    // McOverlay_Start export. Initialization return failures (13) may also
    // leave the normally loaded image resident. The visible LoadLibrary path
    // handles both a new and an already-resident module and reports its own
    // structured failure if re-entry is not possible. Do not fall back for I/O,
    // security, or an unexpected helper failure.
    const bool recoverableJvmAttachFailure =
        isRecoverableJvmAttachFailure(exitCode, m_helperStandardError);
    if (exitStatus == QProcess::NormalExit && recoverableJvmAttachFailure
        && m_loaderKind == LoaderKind::JvmAttach
        && !m_nativeFallbackAttempted) {
        m_jvmAttachFallbackReason =
            QString::fromLocal8Bit(m_helperStandardError).trimmed();
        // Do not immediately launch the second entry point. Agent_OnAttach now
        // queues its work and returns, and old Forge launchers can still report
        // an Attach error before that queued worker reaches the pipe. Starting
        // the native fallback at once used to create two runtimes with the same
        // token; the second stopped the first and appeared as AGENT_DISCONNECTED.
        setState(State::WaitingForAgent);
        setStatusMessage(QStringLiteral(
            "JVM Attach returned a compatibility error; waiting briefly for its asynchronous Agent handshake..."));
        m_nativeFallbackGrace.start();
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString detail = QString::fromLocal8Bit(m_helperStandardError).trimmed();
        const bool nativeLoader = m_loaderKind == LoaderKind::NativeLoadLibrary;
        QString resolvedDetail = detail.isEmpty()
            ? QStringLiteral("%1 exited with code %2.")
                  .arg(nativeLoader ? QStringLiteral("Native DLL loader")
                                    : QStringLiteral("JVM Attach helper"))
                  .arg(exitCode)
            : detail;
        if (nativeLoader && !m_jvmAttachFallbackReason.isEmpty()) {
            resolvedDetail = QStringLiteral("JVM Attach failed first: %1\nNative fallback failed: %2")
                                 .arg(m_jvmAttachFallbackReason, resolvedDetail);
        }
        fail(nativeLoader ? QStringLiteral("NATIVE_DLL_LOAD_FAILED")
                          : QStringLiteral("JVM_ATTACH_FAILED"),
             resolvedDetail);
        return;
    }

    if (!m_authenticated) {
        setState(State::WaitingForAgent);
        setStatusMessage(m_loaderKind == LoaderKind::NativeLoadLibrary
            ? QStringLiteral("Windows loaded the native DLL; waiting for the authenticated agent handshake...")
            : QStringLiteral("JVM accepted the DLL; waiting for the authenticated agent handshake..."));
    }
}

void OverlayManager::handleAttachError(QProcess::ProcessError error)
{
    if (m_closingTransport || error == QProcess::UnknownError
        || m_state == State::Detached
        || m_state == State::Detaching || m_state == State::Error) {
        return;
    }
    fail(m_loaderKind == LoaderKind::NativeLoadLibrary
             ? QStringLiteral("NATIVE_LOADER_PROCESS_ERROR")
             : QStringLiteral("ATTACH_PROCESS_ERROR"),
         m_attachProcess.errorString());
}

void OverlayManager::monitorTarget()
{
    if (m_targetPid != 0 && !targetProcessIsRunning(m_targetPid)) {
        const quint32 exitedPid = m_targetPid;
        m_pendingAttachPid = 0;
        m_detachTimedOut = false;
        m_detachTransportComplete = false;
        setState(State::Detaching);
        completeDetach(false);
        if (m_state == State::Detached)
            setStatusMessage(QStringLiteral("Target JVM exited; native overlay detached"));
        else
            setStatusMessage(QStringLiteral("Target JVM exited; closing the attach helper..."));
        emit targetExited(exitedPid);
    }
}

bool OverlayManager::closeSessionTransport()
{
    if (m_closingTransport)
        return m_attachProcess.state() == QProcess::NotRunning;
    m_closingTransport = true;
    m_attachTimeout.stop();
    m_nativeFallbackGrace.stop();
    m_detachTimeout.stop();
    m_targetMonitor.stop();
    m_authenticated = false;
    m_loaderKind = LoaderKind::None;
    m_agentReadBuffer.clear();

    if (m_agentSocket) {
        disconnect(m_agentSocket, nullptr, this, nullptr);
        m_agentSocket->abort();
        m_agentSocket->deleteLater();
        m_agentSocket = nullptr;
    }
    m_server.close();

    bool helperStopped = true;
    if (m_attachProcess.state() != QProcess::NotRunning) {
        m_attachProcess.kill();
        // kill() is asynchronous. Never spin a nested event loop or block the
        // GUI thread here; handleAttachFinished advances queued re-attachment.
        helperStopped = m_attachProcess.state() == QProcess::NotRunning;
    }
    m_agentDllPath.clear();
    m_agentOptions.clear();
    m_closingTransport = false;
    return helperStopped;
}

void OverlayManager::setState(State state)
{
    if (m_state == state)
        return;
    const bool wasAttached = attached();
    const bool wasBusy = busy();
    m_state = state;
    emit stateChanged();
    if (wasAttached != attached())
        emit attachedChanged();
    if (wasBusy != busy())
        emit busyChanged();
}

void OverlayManager::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void OverlayManager::setRenderer(const QString &renderer)
{
    if (m_renderer == renderer)
        return;
    m_renderer = renderer;
    emit rendererChanged();
}

void OverlayManager::clearError()
{
    if (m_errorCode.isEmpty() && m_errorDetail.isEmpty())
        return;
    m_errorCode.clear();
    m_errorDetail.clear();
    emit errorChanged();
}

void OverlayManager::setErrorState(const QString &code, const QString &detail)
{
    m_errorCode = code;
    m_errorDetail = detail;
    emit errorChanged();
    setState(State::Error);
    setStatusMessage(detail);
}

void OverlayManager::fail(const QString &code, const QString &detail)
{
    (void) closeSessionTransport();
    setRenderer({});
    resetGameState();
    setErrorState(code, detail);
}
