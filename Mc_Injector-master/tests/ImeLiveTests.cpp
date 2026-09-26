// Opt-in interactive Windows TIP test; never runs as part of the regular suite.
#include "overlay_renderer_internal.h"
#include "src/AgentLog.h"
#include <imm.h>
#include <cstdio>
#include <string_view>

// Route the production IME diagnostics to the test transcript.
namespace mcoverlay::log {
void info(std::string_view value) noexcept {std::printf("%.*s\n",static_cast<int>(value.size()),value.data());}
void error(std::string_view value) noexcept {info(value);}
}
namespace mcoverlay {
struct OverlayRendererTestAccess {
    static LRESULT dispatch(OverlayInputState& input,HWND window,UINT message,
                            WPARAM wParam,LPARAM lParam,bool& handled) {
        return OverlayRenderer::onWindowMessage(input,window,message,wParam,lParam,handled);
    }
};
}
namespace {
mcoverlay::OverlayInputState* input=nullptr;
WNDPROC original=nullptr;
unsigned maximumImm=0,maximumTsf=0;
bool typedAll=false;
void observe() {
    if(!input)return;
    maximumImm=std::max(maximumImm,input->imeCandidateCount);
    if(input->tsf) maximumTsf=std::max(maximumTsf,input->tsf->snapshot().count);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    bool handled=false;
    const LRESULT result=mcoverlay::OverlayRendererTestAccess::dispatch(
        *input,window,message,wParam,lParam,handled);
    observe();
    return handled?result:CallWindowProcW(original,window,message,wParam,lParam);
}
void pump(DWORD milliseconds) {
    const ULONGLONG until=GetTickCount64()+milliseconds;
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        observe();Sleep(10);
    } while(GetTickCount64()<until);
}
}
int main(int argc,char** argv) {
    const bool raw=argc>1&&std::string_view(argv[1])=="--raw";
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const HWND previous=GetForegroundWindow();
    const HKL previousLayout=GetKeyboardLayout(0);
    auto state=std::make_unique<mcoverlay::OverlayInputState>();input=state.get();
    input->imeEnabled=true;
    WNDCLASSW rawClass{};rawClass.lpfnWndProc=DefWindowProcW;
    rawClass.hInstance=GetModuleHandleW(nullptr);rawClass.lpszClassName=L"ArcveilImeRawTest";
    RegisterClassW(&rawClass);
    HWND window=CreateWindowExW(0,raw?rawClass.lpszClassName:L"EDIT",L"",WS_OVERLAPPEDWINDOW|ES_MULTILINE,
        120,120,600,220,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 2;
    SetWindowTextW(window,L"");
    original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&procedure)));
    ShowWindow(window,SW_SHOW);
    const DWORD foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
    const DWORD ownThread=GetCurrentThreadId();
    const bool attached=foregroundThread!=ownThread&&AttachThreadInput(ownThread,foregroundThread,TRUE);
    SetForegroundWindow(window);BringWindowToTop(window);SetFocus(window);
    if(attached)AttachThreadInput(ownThread,foregroundThread,FALSE);
    if(raw) {
        ImmAssociateContextEx(window,nullptr,IACE_DEFAULT);
        CreateCaret(window,nullptr,2,20);SetCaretPos(20,40);ShowCaret(window);
    }
    std::printf("LIVE_IME window=%s\n",raw?"raw game-style HWND":"native EDIT");
    SendMessageW(window,WM_NULL,0,0);pump(250);
    // Force a real language transition before enabling the installed Chinese TIP.
    const HKL english=LoadKeyboardLayoutW(L"00000409",0);
    if(english)SendMessageW(window,WM_INPUTLANGCHANGEREQUEST,0,reinterpret_cast<LPARAM>(english));
    pump(150);
    const HKL chinese=LoadKeyboardLayoutW(L"00000804",0);
    if(chinese)SendMessageW(window,WM_INPUTLANGCHANGEREQUEST,0,reinterpret_cast<LPARAM>(chinese));
    ITfInputProcessorProfiles* profiles=nullptr;
    HRESULT activation=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,reinterpret_cast<void**>(&profiles));
    if(SUCCEEDED(activation)) {
        activation=profiles->ChangeCurrentLanguage(0x0804);
        IEnumTfLanguageProfiles* entries=nullptr;
        if(SUCCEEDED(profiles->EnumLanguageProfiles(0x0804,&entries))&&entries) {
            TF_LANGUAGEPROFILE profile{};ULONG fetched=0;
            while(entries->Next(1,&profile,&fetched)==S_OK) {
                BOOL enabled=FALSE;
                profiles->IsEnabledLanguageProfile(profile.clsid,profile.langid,profile.guidProfile,&enabled);
                BSTR description=nullptr;
                profiles->GetLanguageProfileDescription(profile.clsid,profile.langid,profile.guidProfile,&description);
                if(description) {
                    char utf8[512]{};
                    WideCharToMultiByte(CP_UTF8,0,description,-1,utf8,sizeof(utf8),nullptr,nullptr);
                    std::printf("LIVE_IME profile=%s enabled=%d active=%d\n",utf8,enabled?1:0,profile.fActive?1:0);
                    SysFreeString(description);
                }
                if(enabled&&IsEqualGUID(profile.catid,GUID_TFCAT_TIP_KEYBOARD)) {
                    activation=profiles->ActivateLanguageProfile(profile.clsid,profile.langid,profile.guidProfile);
                    if(SUCCEEDED(activation))break;
                }
            }
            entries->Release();
        }
    }
    std::printf("LIVE_IME activation=0x%08lX layout=%p\n",static_cast<unsigned long>(activation),GetKeyboardLayout(0));
    pump(400);
    HIMC ime=ImmGetContext(window);
    if(ime) {
        ImmSetOpenStatus(ime,TRUE);
        ImmSetConversionStatus(ime,IME_CMODE_NATIVE,IME_SMODE_NONE);
        ImmReleaseContext(window,ime);
    }
    pump(250);
    maximumImm=maximumTsf=0;
    typedAll=true;
    for(const char key:std::string_view("NIHAO")) {
        if(GetForegroundWindow()!=window||GetFocus()!=window) {
            std::printf("LIVE_IME focus lost target=%p foreground=%p focus=%p\n",window,GetForegroundWindow(),GetFocus());
            typedAll=false;break;
        }
        INPUT events[2]{};
        events[0].type=events[1].type=INPUT_KEYBOARD;
        events[0].ki.wVk=events[1].ki.wVk=static_cast<WORD>(key);
        events[1].ki.dwFlags=KEYEVENTF_KEYUP;
        if(SendInput(2,events,sizeof(INPUT))!=2) {typedAll=false;break;}
        pump(200);
    }
    pump(1000);
    std::printf("LIVE_IME typed=nihao complete=%d overlayImm=%u overlayTsf=%u\n",
        typedAll?1:0,maximumImm,maximumTsf);
    const bool passed=typedAll&&(maximumImm>0||maximumTsf>0);
    input->imeEnabled=false;
    if(input->tsf)input->tsf->shutdownOnWindowThread();
    SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));
    if(raw)DestroyCaret();
    DestroyWindow(window);state.reset();input=nullptr;
    ActivateKeyboardLayout(previousLayout,0);
    if(profiles)profiles->Release();
    if(IsWindow(previous))SetForegroundWindow(previous);
    if(SUCCEEDED(com))CoUninitialize();
    return passed?0:1;
}
