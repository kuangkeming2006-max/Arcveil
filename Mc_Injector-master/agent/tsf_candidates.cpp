#include "tsf_candidates.h"
#include "src/AgentLog.h"
#include <algorithm>
#include <cwchar>
#include <cstdio>
#include <oleauto.h>

namespace mcoverlay {
namespace {
// mingw-w64's msctf.h omits this Vista interface in some distributions.
// Keep the documented COM ABI locally; do not mix Windows SDK C headers
// into the MinGW runtime include path.
constexpr GUID kCandidateListId{0xea1ea138, 0x19df, 0x11d7,
    {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
// ITfCandidateListUIElement is absent from some mingw-w64 msctf.h versions.
// Match the actual Windows SDK header, not the Learn inheritance summary:
// IUnknown (3 slots), ITfUIElement (4 slots), then the 8 candidate methods.
// https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/msctf.h
using CandidateListElement=detail::CandidateListElement;
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

UINT TsfCandidates::refreshMessage() noexcept
{
    static const UINT value=RegisterWindowMessageW(L"Arcveil.IME.Refresh.v49");
    return value;
}

void TsfCandidates::resetOnWindowThread() noexcept
{
    // Show(FALSE) is ownership we must explicitly return before discarding the
    // generation. Keeping the interface alive avoids querying a transitioning
    // input context from WM_INPUTLANGCHANGE.
    restoreHiddenOnWindowThread();
    ++m_generation;
    m_transitioning=true;
    m_activeId=TF_INVALID_COOKIE;
    AcquireSRWLockExclusive(&m_lock);
    m_snapshot={};
    ReleaseSRWLockExclusive(&m_lock);
    // This message only settles the layout transition after WndProc returns.
    // A UIElement ID from the old generation must never be read later.
    if(m_enabled&&m_window&&!m_refreshQueued)
        m_refreshQueued=PostMessageW(m_window,refreshMessage(),0,0)!=FALSE;
}

void TsfCandidates::refreshOnWindowThread() noexcept
{
    m_refreshQueued=false;
    if(m_transitioning) m_transitioning=false;
}

void TsfCandidates::restoreHiddenOnWindowThread() noexcept
{
    ITfUIElement* const element=m_hiddenElement;
    m_hiddenElement=nullptr;
    m_hiddenId=TF_INVALID_COOKIE;
    if(element) {
        (void)element->Show(TRUE);
        element->Release();
    }
}

void TsfCandidates::enableOnWindowThread(bool enabled,HWND window) noexcept
{
    if (!enabled) {
        if (m_enabled) shutdownOnWindowThread();
        return;
    }
    if (m_attempted) return;
    m_attempted = true;
    m_enabled = true;
    m_window=window;
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
    resetOnWindowThread();
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
    m_window=nullptr;
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
    if (!m_elements || !m_enabled || m_transitioning) return false;
    const auto generation=m_generation;
    if (m_reading.test_and_set(std::memory_order_acquire)) return false;
    struct ReadingScope final {
        std::atomic_flag& flag;
        ~ReadingScope() { flag.clear(std::memory_order_release); }
    } readingScope{m_reading};
    ITfUIElement* element = nullptr;
    char diagnostic[128]{};
    const HRESULT elementHr=m_elements->GetUIElement(id, &element);
    std::snprintf(diagnostic,sizeof(diagnostic),"TSF_GET_ELEMENT hr=0x%08lX",static_cast<unsigned long>(elementHr));
    log::info(diagnostic);
    if (FAILED(elementHr)||!element) return false;
    CandidateListElement* list = nullptr;
    HRESULT hr = element->QueryInterface(kCandidateListId,
                                         reinterpret_cast<void**>(&list));
    std::snprintf(diagnostic,sizeof(diagnostic),"TSF_QI hr=0x%08lX",static_cast<unsigned long>(hr));
    log::info(diagnostic);
    if (FAILED(hr)||!list) {element->Release();return false;}
    ImeCandidates next{};
    UINT total = 0, selection = 0, page = 0, pages = 0;
    hr = list->GetCount(&total);
    std::snprintf(diagnostic,sizeof(diagnostic),"TSF_GET_COUNT hr=0x%08lX count=%u",static_cast<unsigned long>(hr),total);
    log::info(diagnostic);
    if (SUCCEEDED(hr)) hr = list->GetSelection(&selection);
    if(FAILED(hr)||generation!=m_generation||!m_enabled) {
        list->Release();element->Release();return false;
    }
    if(total==0U) selection=0U;
    else selection=std::min(selection,total-1U);
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
    next.active = next.count > 0U;
    list->Release();
    if(generation!=m_generation||!m_enabled||m_transitioning) {
        element->Release();return false;
    }
    // Keep the native candidate UI visible as a fail-open second channel.
    // Hiding it here races the render thread: a successful TSF read does not
    // prove that the fullscreen overlay has presented even one candidate
    // frame. Previously this could leave both channels invisible.
    if(m_hiddenId!=TF_INVALID_COOKIE) restoreHiddenOnWindowThread();
    element->Release();
    if(generation!=m_generation||!m_enabled||m_transitioning) return false;
    m_activeId = id;
    AcquireSRWLockExclusive(&m_lock);
    m_snapshot = next;
    ReleaseSRWLockExclusive(&m_lock);
    return true;
}
HRESULT TsfCandidates::BeginUIElement(DWORD id, BOOL* show)
{
    char diagnostic[128]{};
    std::snprintf(diagnostic,sizeof(diagnostic),"TSF_BEGIN id=%lu transitioning=%d",static_cast<unsigned long>(id),m_transitioning?1:0);
    log::info(diagnostic);
    m_transitioning=false; // A live TIP callback supersedes deferred settle.
    if (!show) return E_POINTER;
    *show=TRUE;
    if(!m_enabled) return S_OK;
    // Begin is deliberately bookkeeping-only. Calling back into the TIP while it
    // is constructing the element can re-enter TSF and corrupt its COM stack.
    m_activeId = id;
    AcquireSRWLockExclusive(&m_lock);
    m_snapshot = {};
    ReleaseSRWLockExclusive(&m_lock);
    return S_OK;
}
HRESULT TsfCandidates::UpdateUIElement(DWORD id)
{
    char diagnostic[128]{};
    std::snprintf(diagnostic,sizeof(diagnostic),"TSF_UPDATE id=%lu transitioningBefore=%d",static_cast<unsigned long>(id),m_transitioning?1:0);
    log::info(diagnostic);
    m_transitioning=false;
    // The TIP guarantees this ID is live only during its Update callback.
    // read() guards against re-entry and generation changes.
    if(m_enabled) (void)read(id);
    return S_OK;
}
HRESULT TsfCandidates::EndUIElement(DWORD id)
{
    char diagnostic[80]{};
    std::snprintf(diagnostic,sizeof(diagnostic),"TSF_END id=%lu",static_cast<unsigned long>(id));
    log::info(diagnostic);
    if(id==m_hiddenId) restoreHiddenOnWindowThread();
    if (id == m_activeId) {
        m_activeId = TF_INVALID_COOKIE;
        AcquireSRWLockExclusive(&m_lock);
        m_snapshot = {};
        ReleaseSRWLockExclusive(&m_lock);
    }
    return S_OK;
}
}
