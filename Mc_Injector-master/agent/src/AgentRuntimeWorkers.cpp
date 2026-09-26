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

bool AgentRuntime::launchResolver() noexcept
{
    unsigned threadId = 0U;
    m_resolver = reinterpret_cast<HANDLE>(::_beginthreadex(
        nullptr, 0U, &AgentRuntime::resolverEntry, this, 0U, &threadId));
    if (m_resolver == nullptr) {
        m_bindings->markResolverUnavailable();
        log::error("Could not create the asynchronous Minecraft mapping resolver.");
        return false;
    }
    return true;
}

unsigned __stdcall AgentRuntime::resolverEntry(void* const context) noexcept
{
    static_cast<AgentRuntime*>(context)->resolverMain();
    return 0U;
}

void AgentRuntime::resolverMain() noexcept
{
    // ScopedThreadEnv uses AttachCurrentThreadAsDaemon for this CRT-created
    // native thread. Its JNIEnv is resolver-local and is never shared with the
    // Java-owned LWJGL render thread.
    jvm::ScopedThreadEnv environment(m_vm, true);
    if (!environment) {
        m_bindings->markResolverUnavailable();
        log::error("Could not attach the mapping resolver to the JVM.");
        return;
    }
    m_bindings->runResolver(environment.get(), m_stopEvent);
}

void AgentRuntime::joinResolver() noexcept
{
    if (m_resolver != nullptr && ::GetCurrentThreadId() != ::GetThreadId(m_resolver)) {
        ::WaitForSingleObject(m_resolver, INFINITE);
    }
}

bool AgentRuntime::launchBedScanner() noexcept
{
    unsigned threadId = 0U;
    m_bedScanner = reinterpret_cast<HANDLE>(::_beginthreadex(
        nullptr, 0U, &AgentRuntime::bedScannerEntry, this, 0U, &threadId));
    if (m_bedScanner == nullptr) {
        log::error("Could not create the chunk-diff bed scanner thread.");
        return false;
    }
    return true;
}

unsigned __stdcall AgentRuntime::bedScannerEntry(void* const context) noexcept
{
    static_cast<AgentRuntime*>(context)->bedScannerMain();
    return 0U;
}

void AgentRuntime::bedScannerMain() noexcept
{
    jvm::ScopedThreadEnv environment(m_vm, true);
    if (!environment) {
        log::error("Could not attach the chunk-diff bed scanner to the JVM.");
        return;
    }
    m_bindings->runBedScanner(environment.get(), m_stopEvent);
}

void AgentRuntime::joinBedScanner() noexcept
{
    if (m_bedScanner != nullptr &&
        ::GetCurrentThreadId() != ::GetThreadId(m_bedScanner)) {
        ::WaitForSingleObject(m_bedScanner, INFINITE);
    }
}

} // namespace mcoverlay
