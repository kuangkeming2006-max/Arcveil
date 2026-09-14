#include "ProcessScanner.h"

#include <QLocale>
#include <QTimer>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <tlhelp32.h>
#  include <psapi.h>
#endif

namespace {

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

struct WindowCandidate {
    HWND handle = nullptr;
    QString title;
    int score = -1;
};

using WindowMap = QHash<DWORD, WindowCandidate>;

QString windowText(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0)
        return {};

    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, buffer.data(), length + 1);
    return copied > 0 ? QString::fromWCharArray(buffer.data(), copied) : QString{};
}

BOOL CALLBACK collectTopLevelWindow(HWND window, LPARAM context)
{
    if (!IsWindowVisible(window))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0)
        return TRUE;

    const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const QString title = windowText(window).trimmed();

    // A visible, unowned, titled application window is usually the actual GLFW
    // or LWJGL Minecraft surface. The score still permits launchers and unusual
    // modded clients that expose a tool/owned window.
    int score = 10;
    if (!title.isEmpty())
        score += 50;
    if (GetWindow(window, GW_OWNER) == nullptr)
        score += 25;
    if ((extendedStyle & WS_EX_TOOLWINDOW) == 0)
        score += 15;
    if (IsIconic(window))
        score -= 5;

    auto *windows = reinterpret_cast<WindowMap *>(context);
    auto existing = windows->find(pid);
    if (existing == windows->end() || score > existing->score)
        windows->insert(pid, WindowCandidate{window, title, score});

    return TRUE;
}

QString executablePath(HANDLE process)
{
    if (!process)
        return {};

    std::array<wchar_t, 32768> path{};
    DWORD pathLength = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &pathLength))
        return {};

    return QString::fromWCharArray(path.data(), static_cast<int>(pathLength));
}

quint64 workingSetBytes(HANDLE process)
{
    if (!process)
        return 0;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(process,
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters))) {
        return 0;
    }

    return static_cast<quint64>(counters.WorkingSetSize);
}

#endif

} // namespace

ProcessScanner::ProcessScanner(QObject *parent)
    : QAbstractListModel(parent)
{
    // StartupLoader starts the first scan after the main window's first frame.
    // Constructing services must not block the initial loading animation.
    connect(&m_scanWatcher, &QFutureWatcher<QVector<ProcessInfo>>::finished, this, [this] {
        m_workerPending = false;
        try {
            applyScanResults(m_scanWatcher.result());
        } catch (...) {
            // Retain the previous usable model if enumeration failed.
            m_scanFailed = true;
        }
        if (!m_continuous || m_scanClock.elapsed() >= 5000) finishScan();
    });
    m_scanTimer.setInterval(50);
    connect(&m_scanTimer, &QTimer::timeout, this, [this] {
        emit scanProgressChanged();
        if (m_scanClock.elapsed() >= 5000) {
            if (!m_workerPending) finishScan();
        } else if (!m_workerPending && m_scanClock.elapsed() - m_lastDispatch >= 500) {
            launchScan();
        }
    });
}

int ProcessScanner::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_processes.size());
}

QVariant ProcessScanner::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_processes.size())
        return {};

    const ProcessInfo &process = m_processes.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case WindowTitleRole:
        return process.windowTitle;
    case PidRole:
        return process.pid;
    case ExecutableNameRole:
        return process.executableName;
    case ExecutablePathRole:
        return process.executablePath;
    case MemoryBytesRole:
        return QVariant::fromValue(process.memoryBytes);
    case MemoryTextRole:
        return formatBytes(process.memoryBytes);
    case WindowHandleRole:
        return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(process.windowHandle));
    case HasWindowRole:
        return process.windowHandle != 0;
    case SelectedRole:
        return process.pid == m_selectedPid;
    default:
        return {};
    }
}

QHash<int, QByteArray> ProcessScanner::roleNames() const
{
    return {
        {PidRole, "pid"},
        {ExecutableNameRole, "executableName"},
        {ExecutablePathRole, "executablePath"},
        {WindowTitleRole, "windowTitle"},
        {MemoryBytesRole, "memoryBytes"},
        {MemoryTextRole, "memoryText"},
        {WindowHandleRole, "windowHandle"},
        {HasWindowRole, "hasWindow"},
        {SelectedRole, "selected"}
    };
}

QString ProcessScanner::lastRefresh() const
{
    return m_lastRefresh.isValid()
        ? QLocale().toString(m_lastRefresh.time(), QLocale::ShortFormat)
        : QStringLiteral("Never");
}

void ProcessScanner::refresh()
{
    startScan(true);
}

void ProcessScanner::refreshOnce()
{
    startScan(false);
}

double ProcessScanner::scanProgress() const
{
    return m_scanClock.isValid() ? std::clamp(m_scanClock.elapsed()/5000.0,0.0,1.0) : 0;
}

void ProcessScanner::startScan(bool continuous)
{
    if (m_refreshing)
        return;

    m_continuous = continuous;
    m_scanFailed = false;
    m_scanClock.restart();
    if (continuous) m_scanTimer.start();
    setRefreshing(true);
    emit scanProgressChanged();
    setStatusMessage(QStringLiteral("Scanning Windows processes…"));
    launchScan();
}

void ProcessScanner::launchScan()
{
    m_workerPending = true;
    m_lastDispatch = m_scanClock.elapsed();

    // The worker owns only value data; it captures no QObject or model pointer.
    // Slow process/path queries cannot occupy the GUI thread. The watcher
    // publishes the finished result on the model's own thread.
    m_scanWatcher.setFuture(QtConcurrent::run(&ProcessScanner::enumerateJavaProcesses));
}

void ProcessScanner::finishScan()
{
    m_scanTimer.stop();
    setRefreshing(false);
    emit scanProgressChanged();
    setStatusMessage(m_scanFailed
        ? QStringLiteral("Some process queries failed. Refresh to retry.")
        : m_processes.isEmpty() ? QStringLiteral("No running Java processes found")
        : QStringLiteral("Found %1 Java process%2").arg(m_processes.size())
            .arg(m_processes.size()==1 ? QString{} : QStringLiteral("es")));
}

void ProcessScanner::applyScanResults(QVector<ProcessInfo> discovered)
{
    const int previousCount = m_processes.size();
    const quint32 previousSelection = m_selectedPid;

    // Preserve delegates/hover/selection on repeated polls; memory changing
    // must not tear down all cards ten times during the scan animation.
    const bool sameOrder = discovered.size()==m_processes.size() && std::equal(
        discovered.cbegin(), discovered.cend(), m_processes.cbegin(),
        [](const auto& a,const auto& b) { return a.pid==b.pid; });
    if (sameOrder) {
        for (int row=0; row<discovered.size(); ++row) {
            if (discovered[row]==m_processes[row]) continue;
            m_processes[row]=std::move(discovered[row]);
            emit dataChanged(index(row,0),index(row,0));
        }
    } else {
        beginResetModel();
        m_processes = std::move(discovered);
        endResetModel();
    }

    const bool selectionStillExists = std::any_of(
        m_processes.cbegin(), m_processes.cend(),
        [previousSelection](const ProcessInfo &process) {
            return previousSelection != 0 && process.pid == previousSelection;
        });

    if (!selectionStillExists)
        m_selectedPid = 0;

    if (previousCount != m_processes.size())
        emit countChanged();
    if (previousSelection != m_selectedPid)
        emit selectedPidChanged();

    m_lastRefresh = QDateTime::currentDateTime();
    emit lastRefreshChanged();

    setStatusMessage(m_continuous && m_scanClock.elapsed()<5000
        ? QStringLiteral("Scanning… %1 found").arg(m_processes.size())
        : m_processes.isEmpty()
        ? QStringLiteral("No running Java processes found")
        : QStringLiteral("Found %1 Java process%2")
              .arg(m_processes.size())
              .arg(m_processes.size() == 1 ? QString{} : QStringLiteral("es")));
}

void ProcessScanner::selectProcess(quint32 pid)
{
    if (pid == m_selectedPid)
        return;

    const auto match = std::find_if(m_processes.cbegin(), m_processes.cend(),
                                    [pid](const ProcessInfo &process) {
                                        return process.pid == pid;
                                    });
    if (pid != 0 && match == m_processes.cend())
        return;

    const quint32 previousPid = m_selectedPid;
    m_selectedPid = pid;

    for (int row = 0; row < m_processes.size(); ++row) {
        const quint32 rowPid = m_processes.at(row).pid;
        if (rowPid == previousPid || rowPid == m_selectedPid) {
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed, {SelectedRole});
        }
    }

    emit selectedPidChanged();
}

QVariantMap ProcessScanner::processForPid(quint32 pid) const
{
    const auto match = std::find_if(m_processes.cbegin(), m_processes.cend(),
                                    [pid](const ProcessInfo &process) {
                                        return process.pid == pid;
                                    });
    return match == m_processes.cend() ? QVariantMap{} : toVariantMap(*match);
}

QVector<ProcessScanner::ProcessInfo> ProcessScanner::enumerateJavaProcesses()
{
    QVector<ProcessInfo> processes;

#ifdef Q_OS_WIN
    WindowMap windows;
    EnumWindows(collectTopLevelWindow, reinterpret_cast<LPARAM>(&windows));

    const ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid())
        return processes;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry))
        return processes;

    do {
        const bool isJava = _wcsicmp(entry.szExeFile, L"java.exe") == 0
                         || _wcsicmp(entry.szExeFile, L"javaw.exe") == 0;
        if (!isJava)
            continue;

        // Start with full query access for memory counters. Restricted JVMs may
        // reject it, so fall back to limited query access and still report PID/path.
        ScopedHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                         FALSE, entry.th32ProcessID));
        ScopedHandle limitedProcess(process.valid()
            ? nullptr
            : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                          FALSE, entry.th32ProcessID));
        HANDLE queryHandle = process.valid() ? process.get() : limitedProcess.get();

        ProcessInfo info;
        info.pid = entry.th32ProcessID;
        info.executableName = QString::fromWCharArray(entry.szExeFile);
        info.executablePath = executablePath(queryHandle);
        if (info.executablePath.isEmpty())
            info.executablePath = info.executableName;
        info.memoryBytes = process.valid() ? workingSetBytes(process.get()) : 0;

        const auto window = windows.constFind(entry.th32ProcessID);
        if (window != windows.cend()) {
            info.windowHandle = reinterpret_cast<quintptr>(window->handle);
            info.windowTitle = window->title;
        }
        if (info.windowTitle.isEmpty())
            info.windowTitle = QStringLiteral("Java process %1").arg(info.pid);

        processes.push_back(std::move(info));
    } while (Process32NextW(snapshot.get(), &entry));

    std::sort(processes.begin(), processes.end(), [](const ProcessInfo &left,
                                                      const ProcessInfo &right) {
        const bool leftHasWindow = left.windowHandle != 0;
        const bool rightHasWindow = right.windowHandle != 0;
        if (leftHasWindow != rightHasWindow)
            return leftHasWindow > rightHasWindow;
        const int titleOrder = QString::localeAwareCompare(left.windowTitle, right.windowTitle);
        return titleOrder == 0 ? left.pid < right.pid : titleOrder < 0;
    });
#endif

    return processes;
}

QString ProcessScanner::formatBytes(quint64 bytes)
{
    if (bytes == 0)
        return QStringLiteral("Unavailable");

    constexpr double kibibyte = 1024.0;
    constexpr double mebibyte = kibibyte * 1024.0;
    constexpr double gibibyte = mebibyte * 1024.0;

    const QLocale locale;
    if (bytes >= static_cast<quint64>(gibibyte))
        return locale.toString(bytes / gibibyte, 'f', 2) + QStringLiteral(" GiB");
    return locale.toString(bytes / mebibyte, 'f', 0) + QStringLiteral(" MiB");
}

QVariantMap ProcessScanner::toVariantMap(const ProcessInfo &process) const
{
    return {
        {QStringLiteral("pid"), process.pid},
        {QStringLiteral("executableName"), process.executableName},
        {QStringLiteral("executablePath"), process.executablePath},
        {QStringLiteral("windowTitle"), process.windowTitle},
        {QStringLiteral("memoryBytes"), QVariant::fromValue(process.memoryBytes)},
        {QStringLiteral("memoryText"), formatBytes(process.memoryBytes)},
        {QStringLiteral("windowHandle"),
         QVariant::fromValue<qulonglong>(static_cast<qulonglong>(process.windowHandle))},
        {QStringLiteral("hasWindow"), process.windowHandle != 0},
        {QStringLiteral("selected"), process.pid == m_selectedPid}
    };
}

void ProcessScanner::setRefreshing(bool refreshing)
{
    if (m_refreshing == refreshing)
        return;
    m_refreshing = refreshing;
    emit refreshingChanged();
}

void ProcessScanner::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}
