#include "overlay_renderer_internal.h"
#include "UiColors.h"
#include "FeatureNavigation.h"
#include "assets/KenneyInputPromptsResource.h"
#include <span>

#include "src/AgentLog.h"
#include "tsf_candidates.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_win32.h>

#include <gl/GL.h>
#include <imm.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

#include "stb_image.h"

// Dear ImGui intentionally keeps this declaration inside `#if 0` in the
// backend header so including it does not force windows.h on every consumer.
// This translation unit already includes Win32 types through our headers.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace mcoverlay {

namespace renderer_detail {

UINT imeShutdownMessage() noexcept
{
    static const UINT id = RegisterWindowMessageW(L"McOverlay.IME.Stop.v1");
    return id;
}

void stopTsf(HWND window, OverlayInputState* input) noexcept
{
    if (!input || !input->tsf) return;
    input->imeEnabled.store(false, std::memory_order_release);
    DWORD_PTR result = 0;
    // No renderer lock is acquired by the recipient. On a failed/hung HWND
    // the ref-counted COM sink stays alive rather than leaving a dangling callback.
    if (IsWindow(window)) SendMessageTimeoutW(window, imeShutdownMessage(), 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result);
}

void clearImeComposition(OverlayInputState& input) noexcept
{
    ::AcquireSRWLockExclusive(&input.imeLock);
    input.imeComposition={};
    input.imeCandidates={};
    input.imeCandidateCount=0U;
    input.imeCandidateSelection=0U;
    input.imeComposing=false;
    ::ReleaseSRWLockExclusive(&input.imeLock);
    input.imeRevision.fetch_add(1U,std::memory_order_release);
    input.composingInput.store(false,std::memory_order_release);
}

void updateImeState(OverlayInputState& input, const HWND window,
                    const ImeMessageAction action,const LPARAM lParam) noexcept
{
    if(action==ImeMessageAction::Ignore) return;
    if(action==ImeMessageAction::ResetComposition) {
        clearImeComposition(input);
        return;
    }
    std::array<wchar_t, 80U> name{};
    std::array<wchar_t, 128U> composition{};
    std::array<std::array<wchar_t, 64U>, 9U> candidates{};
    std::uint32_t candidateCount = 0U;
    std::uint32_t candidateSelection = 0U;
    bool composing = false;
    if(action==ImeMessageAction::QueryComposition||
       action==ImeMessageAction::QueryCandidates||
       action==ImeMessageAction::ClearCandidates) {
        ::AcquireSRWLockShared(&input.imeLock);
        candidates=input.imeCandidates;
        candidateCount=input.imeCandidateCount;
        candidateSelection=input.imeCandidateSelection;
        ::ReleaseSRWLockShared(&input.imeLock);
    }
    if(action==ImeMessageAction::ClearCandidates) {
        candidates={};candidateCount=0U;candidateSelection=0U;
    }

    DWORD processId = 0U;
    const DWORD threadId = ::GetWindowThreadProcessId(window, &processId);
    const HKL layout = action == ImeMessageAction::ResetLayout
        ? reinterpret_cast<HKL>(lParam) : ::GetKeyboardLayout(threadId);
    if (layout != nullptr) {
        const UINT described = ::ImmGetDescriptionW(
            layout, name.data(), static_cast<UINT>(name.size()));
        if (described == 0U) {
            const LANGID language = LOWORD(reinterpret_cast<ULONG_PTR>(layout));
            (void)::GetLocaleInfoW(MAKELCID(language, SORT_DEFAULT),
                LOCALE_SLOCALIZEDDISPLAYNAME, name.data(),
                static_cast<int>(name.size()));
        }
    }

    // Layout changes are reset-only. The original WndProc has not completed
    // its input-context transition yet, so touching HIMC here can observe or
    // retain the old composition/candidate list.
    HIMC const ime = action!=ImeMessageAction::ResetLayout
        ? ::ImmGetContext(window) : nullptr;
    if (ime != nullptr) {
        const LONG bytes = ::ImmGetCompositionStringW(
            ime, GCS_COMPSTR, composition.data(),
            static_cast<DWORD>((composition.size() - 1U) * sizeof(wchar_t)));
        char diagnostic[160]{};
        std::snprintf(diagnostic,sizeof(diagnostic),"IMM_COMP bytes=%ld",static_cast<long>(bytes));
        log::info(diagnostic);
        if (bytes > 0) {
            composition[std::min<std::size_t>(
                static_cast<std::size_t>(bytes) / sizeof(wchar_t),
                composition.size() - 1U)] = L'\0';
            composing = true;
        }

        if(action==ImeMessageAction::QueryCandidates||
           action==ImeMessageAction::QueryComposition) {
        candidates={};candidateCount=0U;candidateSelection=0U;
        alignas(CANDIDATELIST) std::array<unsigned char, 8192U> candidateBytes{};
        // Composition lParam contains GCS flags, not a candidate-list mask.
        const DWORD candidateMask=action==ImeMessageAction::QueryCandidates
            ?static_cast<DWORD>(lParam):0U;
        DWORD candidateIndex=0U;
        if(candidateMask!=0U) {
            while(candidateIndex<31U &&
                  (candidateMask&(1U<<candidateIndex))==0U) ++candidateIndex;
        }
        const DWORD required = ::ImmGetCandidateListW(ime,candidateIndex,nullptr,0U);
        DWORD totalCandidates=0U,selection=0U;
        if (required >= sizeof(CANDIDATELIST) &&
            required <= candidateBytes.size()) {
            auto* const list = reinterpret_cast<CANDIDATELIST*>(
                candidateBytes.data());
            if (::ImmGetCandidateListW(ime,candidateIndex,list,
                    static_cast<DWORD>(candidateBytes.size())) > 0U) {
                totalCandidates=list->dwCount;selection=list->dwSelection;
                const DWORD pageStart = std::min(list->dwPageStart, list->dwCount);
                const DWORD pageCount = std::min<DWORD>(
                    std::min(list->dwPageSize, list->dwCount - pageStart),
                    static_cast<DWORD>(candidates.size()));
                for (DWORD index = 0U; index < pageCount; ++index) {
                    const DWORD sourceIndex = pageStart + index;
                    if (offsetof(CANDIDATELIST, dwOffset) +
                        (static_cast<std::size_t>(sourceIndex) + 1U) *
                            sizeof(DWORD) > required) continue;
                    const DWORD offset = list->dwOffset[sourceIndex];
                    if (offset >= required) continue;
                    const auto* const source = reinterpret_cast<const wchar_t*>(
                        candidateBytes.data() + offset);
                    std::size_t length = 0U;
                    const std::size_t availableCharacters =
                        (required - offset) / sizeof(wchar_t);
                    while (length + 1U < candidates[index].size() &&
                           length < availableCharacters &&
                           source[length] != L'\0') {
                        candidates[index][length] = source[length];
                        ++length;
                    }
                    ++candidateCount;
                }
                if (list->dwSelection >= pageStart &&
                    list->dwSelection < pageStart + pageCount) {
                    candidateSelection = list->dwSelection - pageStart;
                }
                composing = composing || candidateCount != 0U;
            }
        }
        std::snprintf(diagnostic,sizeof(diagnostic),
            "IMM_CAND required=%lu count=%lu selection=%lu",
            static_cast<unsigned long>(required),static_cast<unsigned long>(totalCandidates),
            static_cast<unsigned long>(selection));
        log::info(diagnostic);
        }
        ::ImmReleaseContext(window, ime);
    }
    composing=composing||candidateCount!=0U;

    ::AcquireSRWLockExclusive(&input.imeLock);
    input.imeName = name;
    input.imeComposition = composition;
    input.imeCandidates = candidates;
    input.imeCandidateCount = candidateCount;
    input.imeCandidateSelection = candidateSelection;
    input.imeComposing = composing;
    ::ReleaseSRWLockExclusive(&input.imeLock);
    input.imeRevision.fetch_add(1U, std::memory_order_release);
    input.composingInput.store(composing, std::memory_order_release);
}

bool isMouseMessage(const UINT message) noexcept
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           message == WM_NCMOUSEMOVE || message == WM_NCLBUTTONDOWN ||
           message == WM_NCLBUTTONUP || message == WM_NCRBUTTONDOWN ||
           message == WM_NCRBUTTONUP || message == WM_SETCURSOR ||
           message == WM_INPUT;
}

bool isKeyboardMessage(const UINT message) noexcept
{
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
           message == WM_CHAR || message == WM_SYSCHAR;
}

} // namespace renderer_detail

using namespace renderer_detail;

void OverlayRenderer::pollFallbackInput() noexcept
{
    OverlayInputState* const input = m_inputState;
    if (input == nullptr ||
        input->directImGuiWndProc.load(std::memory_order_acquire) ||
        !input->acceptImGuiMessages.load(std::memory_order_acquire)) {
        return;
    }

    const HWND window = input->window.load(std::memory_order_acquire);
    if (window == nullptr || ::GetForegroundWindow() != window) {
        ImGuiContext* const context = input->imguiContext.load(std::memory_order_acquire);
        if (context != nullptr) {
            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            for (int button = 0; button < 5; ++button) {
                if (input->mouseDown[button]) {
                    io.AddMouseButtonEvent(button, false);
                    input->mouseDown[button] = false;
                }
            }
        }
        input->fallbackPrimed = false;
        input->captureKeysPrimed = false;
        return;
    }

    const unsigned menuHotkey = input->menuHotkey.load(std::memory_order_acquire);
    const bool menuKeyDown = (::GetAsyncKeyState(static_cast<int>(menuHotkey)) & 0x8000) != 0;
    const bool escapeKeyDown = (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    const bool mouseDown[5] = {
        (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0,
        (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0,
    };

    if (!input->fallbackPrimed) {
        // Prime from the current physical state so attaching while a hotkey is
        // already held does not synthesize a toggle edge.
        input->fallbackPrimed = true;
        input->menuKeyDown = menuKeyDown;
        input->escapeKeyDown = escapeKeyDown;
        ImGuiContext* const context = input->imguiContext.load(std::memory_order_acquire);
        if (context != nullptr) {
            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            POINT cursor{};
            if (::GetCursorPos(&cursor) != FALSE &&
                ::ScreenToClient(window, &cursor) != FALSE) {
                io.AddMousePosEvent(static_cast<float>(cursor.x),
                                    static_cast<float>(cursor.y));
            }
            for (int button = 0; button < 5; ++button) {
                io.AddMouseButtonEvent(button, mouseDown[button]);
            }
        }
        for (int button = 0; button < 5; ++button) {
            input->mouseDown[button] = mouseDown[button];
        }
        return;
    }

    bool capturedThisFrame = false;
    if (input->captureHotkey.load(std::memory_order_acquire)) {
        if (!input->captureKeysPrimed) {
            for (unsigned key = 8U; key <= 254U; ++key) {
                input->keyDown[key] =
                    (::GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
            }
            input->captureKeysPrimed = true;
        } else {
            for (unsigned key = 8U; key <= 254U; ++key) {
                const bool down =
                    (::GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
                const bool rising = down && !input->keyDown[key];
                input->keyDown[key] = down;
                if (!rising || key == VK_LBUTTON || key == VK_RBUTTON ||
                    key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2) {
                    continue;
                }
                input->capturedHotkey.store(key, std::memory_order_release);
                input->captureHotkey.store(false, std::memory_order_release);
                input->captureKeysPrimed = false;
                capturedThisFrame = true;
                break;
            }
        }
    } else {
        input->captureKeysPrimed = false;
    }
    if (!capturedThisFrame) {
        if (menuKeyDown && !input->menuKeyDown &&
            !input->composingInput.load(std::memory_order_acquire) &&
            (!input->gameScreenOpen.load(std::memory_order_acquire) ||
             input->interactive.load(std::memory_order_acquire))) {
            input->clickGuiToggle.store(true, std::memory_order_release);
        }
        if (escapeKeyDown && !input->escapeKeyDown &&
            input->interactive.load(std::memory_order_acquire)) {
            input->clickGuiToggle.store(true, std::memory_order_release);
        }
    }
    input->menuKeyDown = menuKeyDown;
    input->escapeKeyDown = escapeKeyDown;

    ImGuiContext* const context = input->imguiContext.load(std::memory_order_acquire);
    if (context != nullptr) {
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();
        POINT cursor{};
        if (::GetCursorPos(&cursor) != FALSE && ::ScreenToClient(window, &cursor) != FALSE) {
            io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
        }
        for (int button = 0; button < 5; ++button) {
            if (input->mouseDown[button] != mouseDown[button]) {
                io.AddMouseButtonEvent(button, mouseDown[button]);
            }
        }
    }
    for (int button = 0; button < 5; ++button) {
        input->mouseDown[button] = mouseDown[button];
    }
}

LRESULT OverlayRenderer::handleWindowMessage(void* const context,
                                             HWND const window,
                                             const UINT message,
                                             const WPARAM wParam,
                                             const LPARAM lParam,
                                             bool& handled) noexcept
{
    return onWindowMessage(*static_cast<OverlayInputState*>(context),
                           window, message, wParam, lParam, handled);
}

LRESULT OverlayRenderer::onWindowMessage(OverlayInputState& input,
                                         HWND const window,
                                         const UINT message,
                                         const WPARAM wParam,
                                         const LPARAM lParam,
                                         bool& handled) noexcept
{
    if (message == imeShutdownMessage() || message == WM_NCDESTROY) {
        if (input.tsf) input.tsf->shutdownOnWindowThread();
        if (message == imeShutdownMessage()) { handled = true; return 0; }
    } else if(message==WM_INPUTLANGCHANGE||message==WM_INPUTLANGCHANGEREQUEST) {
        if(input.tsf) input.tsf->resetOnWindowThread();
    } else if(message==TsfCandidates::refreshMessage()) {
        if(input.tsf&&input.imeEnabled.load(std::memory_order_acquire))
            input.tsf->refreshOnWindowThread();
        handled=true;return 0;
    } else if (input.tsf) {
        input.tsf->enableOnWindowThread(input.imeEnabled.load(std::memory_order_acquire),window);
    }
    const ImeMessageAction imeAction=classifyImeMessage(message,wParam,
        input.imeEnabled.load(std::memory_order_acquire));
    if(imeAction!=ImeMessageAction::Ignore||
       (input.imeEnabled.load(std::memory_order_acquire)&&
        message>=WM_IME_SETCONTEXT&&message<=WM_IME_KEYUP)) {
        char diagnostic[160]{};
        std::snprintf(diagnostic,sizeof(diagnostic),
            "IME_MSG msg=0x%X wParam=0x%llX lParam=0x%llX",message,
            static_cast<unsigned long long>(wParam),static_cast<unsigned long long>(lParam));
        log::info(diagnostic);
    }
    // Read while this IME message still owns its valid HIMC.
    if(imeAction!=ImeMessageAction::Ignore)
        updateImeState(input,window,imeAction,lParam);
    // A GSMTC-rejecting player is controlled through the helper's native
    // media-key fallback. The GUI's blanket keyboard capture used to swallow
    // those keys before DefWindowProc could turn them into WM_APPCOMMAND.
    // Route only OS media commands to Windows; gameplay input stays captured.
    const bool mediaKeyMessage=(message==WM_KEYDOWN||message==WM_KEYUP||
        message==WM_SYSKEYDOWN||message==WM_SYSKEYUP)&&
        isNativeMediaKey(static_cast<unsigned>(wParam));
    const int appCommand=message==WM_APPCOMMAND
        ? GET_APPCOMMAND_LPARAM(lParam) : 0;
    const bool mediaCommand=appCommand>=APPCOMMAND_VOLUME_MUTE&&
        appCommand<=APPCOMMAND_MEDIA_PLAY_PAUSE;
    if(input.interactive.load(std::memory_order_acquire)&&
       !input.captureHotkey.load(std::memory_order_acquire)&&
       (mediaKeyMessage||mediaCommand)) {
        handled=true;
        return ::DefWindowProcW(window,message,wParam,lParam);
    }
    const bool firstKeyDown = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
                              (lParam & (1LL << 30)) == 0;
    if (firstKeyDown) {
        const UINT scanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
        const UINT extendedScan = scanCode | ((lParam & (1LL << 24)) != 0 ? 0xE000U : 0U);
        const UINT resolvedKey = ::MapVirtualKeyW(extendedScan, MAPVK_VSC_TO_VK_EX);
        const unsigned eventKey = resolvedKey != 0U
            ? resolvedKey : static_cast<unsigned>(wParam);
        if (input.captureHotkey.exchange(false, std::memory_order_acq_rel)) {
            if (eventKey >= 8U && eventKey <= 254U &&
                eventKey != VK_LBUTTON && eventKey != VK_RBUTTON &&
                eventKey != VK_MBUTTON && eventKey != VK_XBUTTON1 &&
                eventKey != VK_XBUTTON2) {
                input.capturedHotkey.store(eventKey, std::memory_order_release);
            }
            handled = true;
            return 1;
        }
        if(!input.interactive.load(std::memory_order_acquire) &&
           !input.gameScreenOpen.load(std::memory_order_acquire) &&
           !input.composingInput.load(std::memory_order_acquire)) {
            const auto matches=[&](const int key) noexcept {
                return key>=8 && key<=254 &&
                    (static_cast<unsigned>(wParam)==static_cast<unsigned>(key) ||
                     eventKey==static_cast<unsigned>(key));
            };
            MediaAction action=MediaAction::None;
            if(matches(input.mediaPreviousHotkey.load(std::memory_order_acquire)))
                action=MediaAction::Previous;
            else if(matches(input.mediaToggleHotkey.load(std::memory_order_acquire)))
                action=MediaAction::Toggle;
            else if(matches(input.mediaNextHotkey.load(std::memory_order_acquire)))
                action=MediaAction::Next;
            if(action!=MediaAction::None) {
                const int key=action==MediaAction::Previous
                    ? input.mediaPreviousHotkey.load(std::memory_order_acquire)
                    : action==MediaAction::Toggle
                        ? input.mediaToggleHotkey.load(std::memory_order_acquire)
                        : input.mediaNextHotkey.load(std::memory_order_acquire);
                // Native transport keys are already consumed by Windows. A
                // custom key must be forwarded once through the helper.
                if(key!=VK_MEDIA_PREV_TRACK && key!=VK_MEDIA_PLAY_PAUSE &&
                   key!=VK_MEDIA_NEXT_TRACK) {
                    // Dispatch is owned by the physical-edge reader, not by
                    // both WndProc and render polling (which doubled actions).
                    // A configured ordinary key belongs to the media binding;
                    // do not also deliver it to Minecraft's gameplay input.
                    handled=true;
                    return 1;
                }
            }
        }
        if (wParam == VK_ESCAPE && input.interactive.load(std::memory_order_acquire)) {
            if (::GetCapture() == window) ::ReleaseCapture();
            input.clickGuiToggle.store(true, std::memory_order_release);
            handled = true;
            return 1;
        }
        const unsigned configured = input.menuHotkey.load(std::memory_order_acquire);
        if ((static_cast<unsigned>(wParam) == configured || resolvedKey == configured) &&
            !input.composingInput.load(std::memory_order_acquire) &&
            (!input.gameScreenOpen.load(std::memory_order_acquire) ||
             input.interactive.load(std::memory_order_acquire))) {
            if (input.interactive.load(std::memory_order_acquire) &&
                ::GetCapture() == window) {
                ::ReleaseCapture();
            }
            input.clickGuiToggle.store(true, std::memory_order_release);
            handled = true;
            return 1;
        }
    }
    ImGuiContext* const context = input.imguiContext.load(std::memory_order_acquire);
    const bool directBackend =
        input.directImGuiWndProc.load(std::memory_order_acquire);
    if (input.acceptImGuiMessages.load(std::memory_order_acquire) &&
        input.interactive.load(std::memory_order_acquire) && context != nullptr &&
        directBackend) {
        ImGui::SetCurrentContext(context);
        (void)ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    }
    // When the Click GUI is open Minecraft must not receive any relative/raw
    // movement or gameplay key, even if ImGui currently has no hovered item.
    // WM_INPUT is cleaned up through DefWindowProc but never forwarded into the
    // client WndProc. This is what prevents Lunar's camera from rotating.
    const bool clientCursorMessage = message == WM_SETCURSOR &&
        LOWORD(lParam) == HTCLIENT;
    const bool overlayMouseMessage = isMouseMessage(message) &&
        (message != WM_SETCURSOR || clientCursorMessage);
    const bool interactiveNow = input.interactive.load(std::memory_order_acquire);
    if (interactiveNow && !directBackend) {
        // The official backend performs this exact capture transition on its
        // owning thread. Lunar's split presentation thread cannot call that
        // backend from WndProc, so mirror only the Win32 capture portion here.
        // This keeps drag delivery continuous when the pointer crosses a card
        // or the game client boundary; position still comes from one absolute
        // GetCursorPos source on the render thread.
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
            message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
            if (::GetCapture() == nullptr) ::SetCapture(window);
        } else if (message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
                   message == WM_MBUTTONUP || message == WM_XBUTTONUP) {
            const bool anyButtonDown =
                (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0 ||
                (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
            if (!anyButtonDown && ::GetCapture() == window) ::ReleaseCapture();
        }
    }
    if (clientCursorMessage && interactiveNow) {
        // Preserve the exact cursor Minecraft exposed when the GUI opened.
        // Search fields intentionally have no cursor override, including no
        // I-beam and no per-mouse-move SetCursor race on Lunar.
        if (HCURSOR const cursor=input.sessionCursor.load(std::memory_order_acquire);
            cursor!=nullptr && ::GetCursor()!=cursor) {
            ::SetCursor(cursor);
        }
        handled=true;
        return TRUE;
    }
    if (interactiveNow &&
        (overlayMouseMessage || isKeyboardMessage(message))) {
        handled = true;
        return message == WM_INPUT
            ? ::DefWindowProcW(window, message, wParam, lParam) : 1;
    }
    handled = false;
    return 0;
}


} // namespace mcoverlay
