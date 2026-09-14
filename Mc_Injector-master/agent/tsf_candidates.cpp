#include "tsf_candidates.h"
#include "src/AgentLog.h"
#include <algorithm>
#include <cwchar>
#include <oleauto.h>

namespace mcoverlay {
namespace {
// mingw-w64's msctf.h omits this Vista interface in some distributions.
// Keep the documented COM ABI locally; do not mix Windows SDK C headers
// into the MinGW runtime include path.
constexpr GUID kCandidateListId{0xea1ea138, 0x19df, 0x11d7,
    {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
// ITfCandidateListUIElement is absent from some mingw-w64 msctf.h versions.
// Spell out the Windows SDK vtable exactly, beginning at IUnknown, instead of
// deriving from ITfUIElement and accidentally shifting the candidate methods.
struct CandidateListElement : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDescription(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGUID(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Show(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsShown(BOOL*) = 0;
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
HRESULT TsfCandidates::QueryInterface(REFIID iid, void** out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_ITfUIElementSink)) {
        *out = static_cast<ITfUIElementSink*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}
ULONG TsfCandidates::AddRef() { return ++m_refs; }
ULONG TsfCandidates::Release() { const ULONG n = --m_refs; if (!n) delete this; return n; }

void TsfCandidates::enableOnWindowThread(bool enabled) noexcept
{
    if (!enabled) {
        if (m_enabled) shutdownOnWindowThread();
        return;
    }
    if (m_attempted) return;
    m_attempted = true;
    m_enabled = true;
    m_thread = GetCurrentThreadId();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return;
    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfThreadMgrEx, reinterpret_cast<void**>(&m_manager));
    if (SUCCEEDED(hr)) {
        TfClientId client = 0;
        hr = m_manager->ActivateEx(&client, TF_TMAE_UIELEMENTENABLEDONLY);
        m_activated = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr)) hr = m_manager->QueryInterface(IID_ITfUIElementMgr,
        reinterpret_cast<void**>(&m_elements));
    ITfSource* source = nullptr;
    if (SUCCEEDED(hr)) hr = m_manager->QueryInterface(IID_ITfSource,
        reinterpret_cast<void**>(&source));
    if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfUIElementSink,
        static_cast<ITfUIElementSink*>(this), &m_cookie);
    if (source) source->Release();
    log::info(SUCCEEDED(hr) ? "TSF candidate sink active on window thread."
                           : "TSF candidate sink unavailable; retaining IMM fallback.");
}

void TsfCandidates::shutdownOnWindowThread() noexcept
{
    if (m_thread && m_thread != GetCurrentThreadId()) return;
    m_enabled = false;
    if (m_elements && m_activeId != TF_INVALID_COOKIE) {
        ITfUIElement* element = nullptr;
        if (SUCCEEDED(m_elements->GetUIElement(m_activeId, &element))) {
            element->Show(TRUE); // return ownership of system UI on disable
            element->Release();
        }
    }
    if (m_manager && m_cookie != TF_INVALID_COOKIE) {
        ITfSource* source = nullptr;
        if (SUCCEEDED(m_manager->QueryInterface(IID_ITfSource,
                reinterpret_cast<void**>(&source)))) {
            source->UnadviseSink(m_cookie);
            source->Release();
        }
    }
    m_cookie = TF_INVALID_COOKIE;
    if (m_elements) m_elements->Release();
    m_elements = nullptr;
    if (m_manager) { if (m_activated) m_manager->Deactivate(); m_manager->Release(); }
    m_manager = nullptr;
    m_activated = false;
    if (m_comInitialized) CoUninitialize();
    m_comInitialized = false;
    m_attempted = false;
    m_thread = 0;
    m_activeId = TF_INVALID_COOKIE;
    AcquireSRWLockExclusive(&m_lock);
    m_snapshot = {};
    ReleaseSRWLockExclusive(&m_lock);
}

ImeCandidates TsfCandidates::snapshot() noexcept
{
    AcquireSRWLockShared(&m_lock);
    ImeCandidates result = m_snapshot;
    ReleaseSRWLockShared(&m_lock);
    return result;
}

bool TsfCandidates::read(DWORD id) noexcept
{
    if (!m_elements || !m_enabled) return false;
    if (m_reading.test_and_set(std::memory_order_acquire)) return false;
    struct ReadingScope final {
        std::atomic_flag& flag;
        ~ReadingScope() { flag.clear(std::memory_order_release); }
    } readingScope{m_reading};
    ITfUIElement* element = nullptr;
    if (FAILED(m_elements->GetUIElement(id, &element))) return false;
    CandidateListElement* list = nullptr;
    HRESULT hr = element->QueryInterface(kCandidateListId,
                                         reinterpret_cast<void**>(&list));
    element->Release();
    if (FAILED(hr)) return false;
    ImeCandidates next{};
    UINT total = 0, selection = 0, page = 0, pages = 0;
    hr = list->GetCount(&total);
    if (SUCCEEDED(hr)) hr = list->GetSelection(&selection);
    (void)list->GetCurrentPage(&page);
    std::array<UINT, 128> starts{};
    const HRESULT pageResult = list->GetPageIndex(starts.data(),
        static_cast<UINT>(starts.size()), &pages);
    UINT start = selection / 9U * 9U;
    UINT end = std::min(total, start + 9U);
    if (SUCCEEDED(pageResult) && page < std::min<UINT>(pages, 128U)) {
        start = std::min(starts[page], total);
        end = page + 1 < std::min<UINT>(pages, 128U)
            ? std::min(starts[page + 1], total) : total;
    }
    // Long IME pages are sliced around the selection so it is always visible.
    if (selection >= start + 9U) start += ((selection - start) / 9U) * 9U;
    end = std::min(end, start + 9U);
    next.pageStart = start;
    if (SUCCEEDED(hr)) {
        for (UINT i = start; i < end; ++i) {
            BSTR value = nullptr;
            if (SUCCEEDED(list->GetString(i, &value)) && value != nullptr) {
                const auto length = std::min<UINT>(SysStringLen(value), 63U);
                std::copy_n(value, length, next.words[i - start].data());
                next.count = i - start + 1;
            }
            SysFreeString(value);
        }
    }
    next.selected = selection >= start ? selection - start : 0U;
    next.active = true;
    list->Release();
    m_activeId = id;
    AcquireSRWLockExclusive(&m_lock);
    m_snapshot = next;
    ReleaseSRWLockExclusive(&m_lock);
    return true;
}
HRESULT TsfCandidates::BeginUIElement(DWORD id, BOOL* show)
{
    if (!show) return E_POINTER;
    // Begin is deliberately bookkeeping-only. Calling back into the TIP while it
    // is constructing the element can re-enter TSF and corrupt its COM stack.
    m_activeId = id;
    AcquireSRWLockExclusive(&m_lock);
    m_snapshot = {};
    ReleaseSRWLockExclusive(&m_lock);
    *show = FALSE;
    return S_OK;
}
HRESULT TsfCandidates::UpdateUIElement(DWORD id) { (void)read(id); return S_OK; }
HRESULT TsfCandidates::EndUIElement(DWORD id)
{
    if (id == m_activeId) {
        m_activeId = TF_INVALID_COOKIE;
        AcquireSRWLockExclusive(&m_lock);
        m_snapshot = {};
        ReleaseSRWLockExclusive(&m_lock);
    }
    return S_OK;
}
}
