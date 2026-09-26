#include "AgentRuntime.h"

#include "AgentLog.h"
#include "bindings/GameBindings.h"
#include "bindings/SmartHotbarPolicy.h"
#include "ipc_client.h"
#include "jvm.h"
#include "opengl_hook.h"
#include "overlay_renderer.h"

#include <process.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>

#include "AgentRuntimeFeatures.internal.h"

namespace mcoverlay {
using namespace runtime_detail;

namespace {
std::mutex g_runtimeMutex;
AgentRuntime* g_runtime = nullptr;

}

AgentRuntime::AgentRuntime(JavaVM* const vm,
                           jvmtiEnv* const jvmti,
                           AgentOptions options)
    : m_vm(vm), m_jvmti(jvmti), m_options(std::move(options))
{
    m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_callbacksIdleEvent = ::CreateEventW(nullptr, TRUE, TRUE, nullptr);
    m_rendererStoppedEvent = ::CreateEventW(nullptr, TRUE, TRUE, nullptr);
    m_telemetryEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_ipc = std::make_unique<IpcClient>(m_options.pipeName);
    m_hook = std::make_unique<OpenGlHook>();
    m_renderer = std::make_unique<OverlayRenderer>();
    m_bindings = std::make_unique<GameBindings>(m_vm, m_jvmti);
}

AgentRuntime::~AgentRuntime()
{
    if (m_telemetry != nullptr) ::CloseHandle(m_telemetry);
    if (m_telemetryEvent != nullptr) ::CloseHandle(m_telemetryEvent);
    if (m_resolver != nullptr) ::CloseHandle(m_resolver);
    if (m_bedScanner != nullptr) ::CloseHandle(m_bedScanner);
    if (m_worker != nullptr) ::CloseHandle(m_worker);
    if (m_rendererStoppedEvent != nullptr) ::CloseHandle(m_rendererStoppedEvent);
    if (m_callbacksIdleEvent != nullptr) ::CloseHandle(m_callbacksIdleEvent);
    if (m_readyEvent != nullptr) ::CloseHandle(m_readyEvent);
    if (m_stopEvent != nullptr) ::CloseHandle(m_stopEvent);
}

jint AgentRuntime::start(JavaVM* const vm, const char* const rawOptions) noexcept
{
    try {
        AgentOptions options = parseAgentOptions(rawOptions);
        jvmtiEnv* const jvmti = jvm::resolveJvmti(vm);
        if (vm == nullptr || jvmti == nullptr || !options.valid()) {
            log::error("Invalid JVM, JVMTI environment, or agent options.");
            return JNI_ERR;
        }

        std::lock_guard lock(g_runtimeMutex);
        if (g_runtime != nullptr) {
            // Agent_OnAttach and the Windows loader fallback may both reach the
            // same asynchronous bootstrap on older Forge VMs. Treat an already
            // running, identical authenticated session as success. Restarting
            // it would close the live pipe and surface as AGENT_DISCONNECTED.
            const bool sameSession = g_runtime->m_options.protocol == options.protocol &&
                g_runtime->m_options.pipeName == options.pipeName &&
                g_runtime->m_options.token == options.token;
            if (sameSession && g_runtime->m_startSucceeded.load(std::memory_order_acquire) &&
                g_runtime->m_running.load(std::memory_order_acquire) &&
                !g_runtime->m_shutdownRequested.load(std::memory_order_acquire)) {
                log::info("Duplicate bootstrap for the active session was ignored.");
                return JNI_OK;
            }
            g_runtime->requestStop(false);
            g_runtime->join();
            if (!g_runtime->shutdownGraphics()) {
                log::error("Previous agent runtime retained because OpenGL hooks could not be removed.");
                return JNI_ERR;
            }
            delete g_runtime;
            g_runtime = nullptr;
        }

        auto* runtime = new AgentRuntime(vm, jvmti, std::move(options));
        if (!runtime->launch()) {
            // A launch timeout can occur after hooks were installed and the
            // worker entered its cleanup path. Apply the same fail-closed
            // lifetime rule as stop(): never free a context that a detour may
            // still reference.
            if (runtime->shutdownGraphics()) {
                delete runtime;
            } else {
                g_runtime = runtime;
                log::error("Failed launch runtime retained because hooks remain active.");
            }
            return JNI_ERR;
        }
        g_runtime = runtime;
        return JNI_OK;
    } catch (...) {
        log::error("Unhandled exception while starting the agent.");
        return JNI_ERR;
    }
}

void AgentRuntime::stop(const bool vmUnloading) noexcept
{
    std::lock_guard lock(g_runtimeMutex);
    AgentRuntime* const runtime = g_runtime;
    if (runtime == nullptr) {
        return;
    }
    runtime->requestStop(vmUnloading);
    runtime->join();
    if (!runtime->shutdownGraphics()) {
        // A detour may still contain this runtime as its callback context.
        // Retaining the complete object graph is safer than freeing a target
        // that a future SwapBuffers call can still dereference.
        log::error("Agent runtime retained because OpenGL hooks are still active.");
        return;
    }
    g_runtime = nullptr;
    delete runtime;
}

bool AgentRuntime::launch() noexcept
{
    if (m_stopEvent == nullptr || m_readyEvent == nullptr ||
        m_callbacksIdleEvent == nullptr || m_rendererStoppedEvent == nullptr) {
        return false;
    }
    unsigned threadId = 0U;
    m_worker = reinterpret_cast<HANDLE>(::_beginthreadex(
        nullptr, 0U, &AgentRuntime::workerEntry, this, 0U, &threadId));
    if (m_worker == nullptr) {
        return false;
    }
    if (::WaitForSingleObject(m_readyEvent, 5000U) != WAIT_OBJECT_0) {
        requestStop(false);
        join();
        return false;
    }
    const bool succeeded = m_startSucceeded.load(std::memory_order_acquire);
    if (!succeeded) {
        join();
    }
    return succeeded;
}

void AgentRuntime::requestStop(const bool vmUnloading) noexcept
{
    m_vmUnloading.store(vmUnloading, std::memory_order_release);
    if (m_stopEvent != nullptr) {
        ::SetEvent(m_stopEvent);
    }
    if (m_ipc != nullptr) {
        m_ipc->cancel();
    }
}

void AgentRuntime::join() noexcept
{
    if (m_worker != nullptr && ::GetCurrentThreadId() != ::GetThreadId(m_worker)) {
        ::WaitForSingleObject(m_worker, INFINITE);
    }
}

unsigned __stdcall AgentRuntime::workerEntry(void* const context) noexcept
{
    static_cast<AgentRuntime*>(context)->workerMain();
    return 0U;
}

void AgentRuntime::workerMain() noexcept
{
    if (!m_hook->install(&AgentRuntime::frameEntry, this)) {
        // The asynchronous Agent_OnAttach entry has already returned JNI_OK,
        // so OutputDebugString alone cannot explain this failure to the Qt
        // controller. Keep a minimal pipe worker alive long enough to deliver
        // an authenticated, structured error. A later Retry can then replace
        // this inert runtime without restarting Minecraft.
        m_running.store(true, std::memory_order_release);
        m_startSucceeded.store(true, std::memory_order_release);
        ::SetEvent(m_readyEvent);
        m_ipc->run(m_stopEvent,
            [this] {
                if (!sendHello() ||
                    !m_ipc->sendLine("ERROR HOOK_INSTALL_FAILED unable-to-hook-gdi32-SwapBuffers")) {
                    m_ipc->cancel();
                }
            },
            [this](const std::string_view line) { return handleControlLine(line); },
            [] {},
            [this] {
                m_visible.store(false, std::memory_order_release);
                m_interactive.store(false, std::memory_order_release);
            });
        m_running.store(false, std::memory_order_release);
        return;
    }
    // Mapping support is optional for rendering. If this thread cannot be
    // created, ImGui still initializes and displays an explicit unavailable
    // data state instead of blocking HOOK_READY/RENDERER_READY.
    (void)launchResolver();
    (void)launchBedScanner();
    (void)launchTelemetry();
    m_running.store(true, std::memory_order_release);
    m_startSucceeded.store(true, std::memory_order_release);
    ::SetEvent(m_readyEvent);

    m_ipc->run(m_stopEvent,
        [this] {
            if (!sendHandshake()) {
                m_ipc->cancel();
            }
        },
        [this](const std::string_view line) { return handleControlLine(line); },
        [this] {
            const bool shutdownComplete = shutdownGraphics();
            if (shutdownComplete && m_detachRequested.load(std::memory_order_acquire)) {
                (void)m_ipc->sendLine("DETACH_COMPLETE");
            } else if (!shutdownComplete) {
                (void)m_ipc->sendLine("ERROR HOOK_DISABLE_FAILED runtime-retained");
            }
        },
        [this] {
            m_visible.store(false, std::memory_order_release);
            m_interactive.store(false, std::memory_order_release);
        });
    m_running.store(false, std::memory_order_release);
}

bool AgentRuntime::shutdownGraphics() noexcept
{
    if (m_cleanupCompleted.load(std::memory_order_acquire)) {
        return true;
    }

    m_visible.store(false, std::memory_order_release);
    m_interactive.store(false, std::memory_order_release);

    // Signal first so a resolver backoff wait exits immediately, then join
    // before any callback is allowed to release the published global refs.
    // A currently executing JVMTI snapshot is allowed to finish; it can no
    // longer race cache destruction afterwards.
    if (m_stopEvent != nullptr) {
        ::SetEvent(m_stopEvent);
    }
    joinResolver();
    joinBedScanner();
    joinTelemetry();

    // Give the owning SwapBuffers/window thread a bounded opportunity to shut
    // down both ImGui backends while the renderer's HGLRC is current. The
    // callback sees m_shutdownRequested before any hook is disabled, so this
    // cannot depend on a detour after its target prologue has been restored.
    // Reset before publication: resetting after the store could erase a signal
    // from a callback that completed between the flag store and ResetEvent.
    ::ResetEvent(m_rendererStoppedEvent);
    m_shutdownRequested.store(true, std::memory_order_release);
    if (!m_renderCleanupCompleted.load(std::memory_order_acquire)) {
        (void)::WaitForSingleObject(m_rendererStoppedEvent, 750U);
    }

    // If MinHook cannot restore every target, the complete runtime remains
    // alive. m_shutdownRequested is intentionally left set: any later detour
    // can finish render-thread cleanup safely, but it never samples JNI state
    // or renders another frame.
    bool disabled = false;
    constexpr unsigned kDisableAttempts = 5U;
    for (unsigned attempt = 0U; attempt < kDisableAttempts; ++attempt) {
        if (m_hook->disable()) {
            disabled = true;
            break;
        }
        ::SwitchToThread();
        if (attempt + 1U < kDisableAttempts) {
            ::Sleep(1U);
        }
    }
    if (!disabled) {
        log::error("OpenGL hooks remain active after bounded disable retries; retaining runtime.");
        return false;
    }

    // remove() drains all detours before clearing callback/trampoline state.
    // If it reports failure, preserve the full graph even though publication
    // may already be cancelled; patched target code still belongs to this DLL.
    if (!m_hook->remove()) {
        log::error("OpenGL hook removal failed; retaining runtime and renderer state.");
        return false;
    }

    ::WaitForSingleObject(m_callbacksIdleEvent, INFINITE);
    // No frame arrived with the old HGLRC (or the context changed). The hook is
    // now disabled and all callbacks are drained, so detach input and retain
    // the backend generation without calling GL teardown or DestroyContext.
    // This is a bounded leak, preferable to issuing glDelete* in another HGLRC
    // or tripping Dear ImGui's backend-lifetime assertion.
    m_renderer->abandonAfterHookDisabled();
    if (m_vmUnloading.load(std::memory_order_acquire)) {
        m_bindings->abandon();
    } else {
        jvm::ScopedThreadEnv cleanupEnvironment(m_vm, true);
        if (cleanupEnvironment) {
            m_bindings->release(cleanupEnvironment.get());
        } else {
            m_bindings->abandon();
        }
    }
    m_cleanupCompleted.store(true, std::memory_order_release);
    ::SetEvent(m_rendererStoppedEvent);
    return true;
}

} // namespace mcoverlay
