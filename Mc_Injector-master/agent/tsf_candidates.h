#pragma once
#include <windows.h>
#include <msctf.h>
#include <array>
#include <atomic>

namespace mcoverlay {
struct ImeCandidates {
    std::array<std::array<wchar_t, 64>, 9> words{};
    unsigned count = 0;
    unsigned selected = 0;
    unsigned pageStart = 0;
    bool active = false;
};

// COM apartment belongs to the HWND thread, not necessarily the WGL thread.
// The render thread only copies the bounded snapshot under a short SRW lock.
class TsfCandidates final : public ITfUIElementSink {
public:
    void enableOnWindowThread(bool enabled) noexcept;
    void shutdownOnWindowThread() noexcept;
    ImeCandidates snapshot() noexcept;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE BeginUIElement(DWORD id, BOOL* show) override;
    HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD id) override;
    HRESULT STDMETHODCALLTYPE EndUIElement(DWORD id) override;
private:
    ~TsfCandidates() = default;
    bool read(DWORD id) noexcept;
    std::atomic<ULONG> m_refs{1};
    SRWLOCK m_lock = SRWLOCK_INIT;
    ImeCandidates m_snapshot{};
    ITfThreadMgrEx* m_manager = nullptr;
    ITfUIElementMgr* m_elements = nullptr;
    DWORD m_cookie = TF_INVALID_COOKIE;
    DWORD m_activeId = TF_INVALID_COOKIE;
    DWORD m_thread = 0;
    bool m_enabled = false;
    bool m_activated = false;
    bool m_comInitialized = false;
    bool m_attempted = false;
    std::atomic_flag m_reading = ATOMIC_FLAG_INIT;
};
}
