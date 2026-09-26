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
QString firstExistingFile(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile())
            return QDir::cleanPath(info.absoluteFilePath());
    }
    return {};
}

bool modularRuntimeContainsAttach(const QDir &runtimeRoot)
{
    if (QFileInfo::exists(runtimeRoot.filePath(QStringLiteral("jmods/jdk.attach.jmod"))))
        return true;

    // A bundled jlink image has no jmods directory. Its release metadata lists
    // the modules retained in lib/modules, so an arbitrary trimmed runtime is
    // not mistaken for an Attach-capable JDK image.
    QFile releaseFile(runtimeRoot.filePath(QStringLiteral("release")));
    if (!releaseFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray release = releaseFile.readAll();
    return release.contains("jdk.attach")
        && QFileInfo::exists(runtimeRoot.filePath(QStringLiteral("lib/modules")));
}

#ifdef Q_OS_WIN

class ScopedHandle final
{
public:
    explicit ScopedHandle(HANDLE handle = nullptr) noexcept : m_handle(handle) {}
    ~ScopedHandle()
    {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }

    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return m_handle; }
    [[nodiscard]] bool valid() const noexcept
    {
        return m_handle && m_handle != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_handle = nullptr;
};

struct WindowSearch {
    DWORD pid = 0;
    HWND best = nullptr;
    int score = -1;
};

BOOL CALLBACK findTargetWindow(HWND window, LPARAM context)
{
    auto *search = reinterpret_cast<WindowSearch *>(context);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid || !IsWindowVisible(window))
        return TRUE;

    int score = GetWindowTextLengthW(window) > 0 ? 50 : 0;
    if (GetWindow(window, GW_OWNER) == nullptr)
        score += 25;
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0)
        score += 15;
    if (score > search->score) {
        search->score = score;
        search->best = window;
    }
    return TRUE;
}

QString nativeWindowTitle(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0)
        return {};

    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, text.data(), length + 1);
    return copied > 0 ? QString::fromWCharArray(text.data(), copied) : QString{};
}

#endif

}

bool OverlayManager::isRecoverableJvmAttachFailure(
    const int exitCode, const QByteArray &standardError) noexcept
{
    if (exitCode == 10 || exitCode == 12 || exitCode == 13)
        return true;

    // JDK 8's Windows Attach listener uses native result 100 for
    // ATTACH_ERROR_DISABLED. JDK 9+ turns that result into InternalError, which
    // older helper builds did not catch and therefore returned as exit 1. Keep
    // this controller-side recognition as a compatibility guard for either
    // helper form. The native fallback still independently requires a live
    // x64 JVM and a visible game window before it loads anything.
    return standardError.contains(
        QByteArrayLiteral("Remote thread failed for unknown reason (100)"));
}

QString OverlayManager::locateAgentDll() const
{
    const QDir appDirectory(QCoreApplication::applicationDirPath());
    return firstExistingFile({
        appDirectory.filePath(QStringLiteral("agent/McOverlayAgent.dll")),
        appDirectory.filePath(QStringLiteral("McOverlayAgent.dll")),
        appDirectory.filePath(QStringLiteral("../agent/McOverlayAgent.dll"))
    });
}

QString OverlayManager::locateAttachHelper() const
{
    const QDir appDirectory(QCoreApplication::applicationDirPath());
    return firstExistingFile({
        appDirectory.filePath(QStringLiteral("tools/McOverlayAttachHelper.jar")),
        appDirectory.filePath(QStringLiteral("McOverlayAttachHelper.jar")),
        appDirectory.filePath(QStringLiteral("../attach-helper/McOverlayAttachHelper.jar"))
    });
}

QString OverlayManager::locateNativeLoader() const
{
    const QDir appDirectory(QCoreApplication::applicationDirPath());
    return firstExistingFile({
        appDirectory.filePath(QStringLiteral("tools/McOverlayNativeLoader.exe")),
        appDirectory.filePath(QStringLiteral("McOverlayNativeLoader.exe")),
        appDirectory.filePath(QStringLiteral("../native-loader/McOverlayNativeLoader.exe"))
    });
}

OverlayManager::JavaRuntime OverlayManager::locateJavaRuntime(
    const QString &targetExecutable) const
{
    const QDir appDirectory(QCoreApplication::applicationDirPath());
    QStringList candidates;
    candidates << appDirectory.filePath(QStringLiteral("runtime/bin/java.exe"));

    const QString javaHome = QProcessEnvironment::systemEnvironment()
                                 .value(QStringLiteral("JAVA_HOME"));
    if (!javaHome.isEmpty())
        candidates << QDir(javaHome).filePath(QStringLiteral("bin/java.exe"));

    const QString pathJava = QStandardPaths::findExecutable(QStringLiteral("java.exe"));
    if (!pathJava.isEmpty())
        candidates << pathJava;

    const QFileInfo targetInfo(targetExecutable);
    candidates << targetInfo.dir().filePath(QStringLiteral("java.exe"));

    for (const QString &candidate : candidates) {
        const QFileInfo javaInfo(candidate);
        if (!javaInfo.isFile())
            continue;

        // java.exe lives in <jdk>/bin. Resolve its parent explicitly instead
        // of relying on the controller's current directory or JAVA_HOME text.
        const QDir runtimeRoot(javaInfo.dir().absoluteFilePath(QStringLiteral("..")));
        const QString toolsJar = runtimeRoot.filePath(QStringLiteral("lib/tools.jar"));

        if (modularRuntimeContainsAttach(runtimeRoot))
            return {javaInfo.absoluteFilePath(), {}, true};
        if (QFileInfo::exists(toolsJar))
            return {javaInfo.absoluteFilePath(), toolsJar, false};
    }
    return {};
}

QString OverlayManager::targetExecutablePath(quint32 pid) const
{
#ifdef Q_OS_WIN
    const ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                           FALSE, pid));
    if (!process.valid())
        return {};

    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length))
        return {};
    return QString::fromWCharArray(path.data(), static_cast<int>(length));
#else
    Q_UNUSED(pid)
    return {};
#endif
}

QString OverlayManager::targetWindowTitle(quint32 pid) const
{
#ifdef Q_OS_WIN
    WindowSearch search{pid};
    EnumWindows(findTargetWindow, reinterpret_cast<LPARAM>(&search));
    return nativeWindowTitle(search.best);
#else
    Q_UNUSED(pid)
    return {};
#endif
}

bool OverlayManager::targetArchitectureSupported(quint32 pid) const
{
#ifdef Q_OS_WIN
    const ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                           FALSE, pid));
    if (!process.valid())
        return false;

    using IsWow64Process2Function = BOOL (WINAPI *)(HANDLE, USHORT *, USHORT *);
    const auto isWow64Process2 = reinterpret_cast<IsWow64Process2Function>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (isWow64Process2) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (!isWow64Process2(process.get(), &processMachine, &nativeMachine))
            return false;
        return processMachine == IMAGE_FILE_MACHINE_UNKNOWN
            && nativeMachine == IMAGE_FILE_MACHINE_AMD64;
    }

    BOOL wow64 = FALSE;
    return IsWow64Process(process.get(), &wow64) && !wow64
        && sizeof(void *) == 8;
#else
    Q_UNUSED(pid)
    return false;
#endif
}

bool OverlayManager::targetProcessIsRunning(quint32 pid) const
{
#ifdef Q_OS_WIN
    const ScopedHandle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (process.valid())
        return WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;

    // A transient access-denied result must not be interpreted as process
    // death. Toolhelp still lets us distinguish a live PID from an exited one.
    const ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid())
        return true;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry))
        return true;
    do {
        if (entry.th32ProcessID == pid)
            return true;
    } while (Process32NextW(snapshot.get(), &entry));
    return false;
#else
    Q_UNUSED(pid)
    return false;
#endif
}

bool OverlayManager::targetHasLoadedJvm(quint32 pid) const
{
#ifdef Q_OS_WIN
    // Restrict the OS-loader fallback to a process that is demonstrably a live
    // x64 JVM. This avoids treating every AttachNotSupportedException as proof
    // that an arbitrary java-named process is a valid native-agent target.
    for (int attempt = 0; attempt < 8; ++attempt) {
        const HANDLE rawSnapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (rawSnapshot == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_BAD_LENGTH)
                continue;
            return false;
        }

        const ScopedHandle snapshot(rawSnapshot);
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (!Module32FirstW(snapshot.get(), &module))
            return false;
        do {
            if (_wcsicmp(module.szModule, L"jvm.dll") == 0)
                return true;
        } while (Module32NextW(snapshot.get(), &module));
        return false;
    }
    return false;
#else
    Q_UNUSED(pid)
    return false;
#endif
}

bool OverlayManager::targetHasLoadedOverlayAgent(quint32 pid) const
{
#ifdef Q_OS_WIN
    for (int attempt = 0; attempt < 8; ++attempt) {
        const HANDLE rawSnapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (rawSnapshot == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_BAD_LENGTH)
                continue;
            return false;
        }

        const ScopedHandle snapshot(rawSnapshot);
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (!Module32FirstW(snapshot.get(), &module))
            return false;
        do {
            if (_wcsicmp(module.szModule, L"McOverlayAgent.dll") == 0)
                return true;
        } while (Module32NextW(snapshot.get(), &module));
        return false;
    }
    return false;
#else
    Q_UNUSED(pid)
    return false;
#endif
}

bool OverlayManager::startNativeLoaderFallback()
{
    m_nativeFallbackGrace.stop();
    m_nativeFallbackAttempted = true;
    if (!targetHasLoadedJvm(m_targetPid)
        || targetWindowTitle(m_targetPid).trimmed().isEmpty()) {
        fail(QStringLiteral("NATIVE_LOADER_TARGET_REJECTED"),
             QStringLiteral("JVM Attach is unavailable, but the selected process does not expose both "
                            "a loaded jvm.dll and a visible game window. The DLL was not loaded."));
        return false;
    }
    const QString nativeLoader = locateNativeLoader();
    if (nativeLoader.isEmpty()) {
        fail(QStringLiteral("NATIVE_LOADER_NOT_FOUND"),
             QStringLiteral("Runtime JVM Attach is unavailable, and "
                            "McOverlayNativeLoader.exe was not found beside the application."));
        return false;
    }
    if (m_agentDllPath.isEmpty() || m_agentOptions.isEmpty() || m_targetPid == 0) {
        fail(QStringLiteral("NATIVE_LOADER_INVALID_SESSION"),
             QStringLiteral("The native loader session was incomplete; select the process and try again."));
        return false;
    }

    m_helperStandardOutput.clear();
    m_helperStandardError.clear();
    m_loaderKind = LoaderKind::NativeLoadLibrary;
    setState(State::LaunchingAttachHelper);
    setStatusMessage(QStringLiteral("JVM Attach did not complete; retrying through the visible native DLL export..."));
    m_attachProcess.setProgram(nativeLoader);
    m_attachProcess.setArguments({QString::number(m_targetPid),
                                  m_agentDllPath,
                                  m_agentOptions});
    m_attachProcess.start();

    // Give the fallback a full handshake window instead of consuming the
    // remainder of the failed Attach attempt's timer.
    m_attachTimeout.start();
    setState(State::WaitingForAgent);
    return true;
}

