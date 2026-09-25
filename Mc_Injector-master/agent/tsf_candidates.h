#pragma once
#include <windows.h>
#include <msctf.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace mcoverlay {
namespace detail {
// Exact Windows SDK ABI, including the four inherited ITfUIElement methods.
struct CandidateListElement : ITfUIElement {
    virtual HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(UINT, BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(UINT*, UINT, UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(UINT*, UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT*) = 0;
};
}
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
    void enableOnWindowThread(bool enabled,HWND window) noexcept;
    static UINT refreshMessage() noexcept;
    void resetOnWindowThread() noexcept;
    void refreshOnWindowThread() noexcept;
    void shutdownOnWindowThread() noexcept;
    ImeCandidates snapshot() noexcept;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE BeginUIElement(DWORD id, BOOL* show) override;
    HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD id) override;
    HRESULT STDMETHODCALLTYPE EndUIElement(DWORD id) override;
private:
    friend struct TsfCandidatesTestAccess;
    ~TsfCandidates() = default;
    bool read(DWORD id) noexcept;
    void restoreHiddenOnWindowThread() noexcept;
    std::atomic<ULONG> m_refs{1};
    SRWLOCK m_lock = SRWLOCK_INIT;
    ImeCandidates m_snapshot{};
    ITfThreadMgrEx* m_manager = nullptr;
    ITfUIElementMgr* m_elements = nullptr;
    DWORD m_cookie = TF_INVALID_COOKIE;
    DWORD m_activeId = TF_INVALID_COOKIE;
    DWORD m_hiddenId = TF_INVALID_COOKIE;
    ITfUIElement* m_hiddenElement = nullptr;
    DWORD m_thread = 0;
    HWND m_window=nullptr;
    std::uint64_t m_generation=0U;
    bool m_refreshQueued=false;
    bool m_transitioning=false;
    bool m_enabled = false;
    bool m_activated = false;
    bool m_comInitialized = false;
    bool m_attempted = false;
    std::atomic_flag m_reading = ATOMIC_FLAG_INIT;
};
}
