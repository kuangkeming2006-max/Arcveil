// Opt-in interactive Windows TIP test; never runs as part of the regular suite.
#include "overlay_renderer_internal.h"
#include "src/AgentLog.h"
#include <imm.h>
#include <gl/GL.h>
#include <QImage>
#include <QDir>
#include <cstdio>
#include <string_view>
#include <thread>

// Route the production IME diagnostics to the test transcript.
namespace mcoverlay::log {
unsigned nativeRestoreRequests=0;
void info(std::string_view value) noexcept {
    if(value.starts_with("TSF_NATIVE_UI restoreRequested=1"))++nativeRestoreRequests;
    std::printf("%.*s\n",static_cast<int>(value.size()),value.data());
}
void error(std::string_view value) noexcept {info(value);}
}
namespace mcoverlay {
struct OverlayRendererTestAccess {
    static OverlayInputState* state(OverlayRenderer& renderer) {return renderer.m_inputState;}
    static std::uint64_t drawnGeneration(OverlayRenderer& renderer) {return renderer.m_imeDrawnGeneration;}
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
std::unique_ptr<mcoverlay::OverlayRenderer> renderer;
std::unique_ptr<mcoverlay::GameSnapshot> snapshot;
HDC renderDc=nullptr;
bool splitRendering=false;
std::atomic<bool> renderingPaused{false},renderStop{false},renderDone{false};
std::atomic<bool> captureRequested{false},captureDone{false},screenshotSaved{true};
std::atomic<mcoverlay::OverlayInputState*> renderInput{nullptr};
std::atomic<unsigned> candidateFrames{0};
QString captureOutput;
std::wstring committed;
void captureFrame() {
    RECT client{};GetClientRect(WindowFromDC(renderDc),&client);
    QImage frame(client.right,client.bottom,QImage::Format_RGBA8888);
    glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,4);
    glReadPixels(0,0,client.right,client.bottom,GL_RGBA,GL_UNSIGNED_BYTE,frame.bits());
    screenshotSaved=frame.flipped(Qt::Vertical).save(captureOutput+"/ime-live-candidates.png");
    captureDone=true;
}
void draw() {
    if(!renderer||renderingPaused)return;
    RECT client{};GetClientRect(WindowFromDC(renderDc),&client);
    glViewport(0,0,client.right,client.bottom);
    glClearColor(.12F,.16F,.20F,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    (void)renderer->render(renderDc,*snapshot,false);
    if(mcoverlay::OverlayRendererTestAccess::drawnGeneration(*renderer))++candidateFrames;
    if(captureRequested.exchange(false))captureFrame();
    SwapBuffers(renderDc);
}
LRESULT CALLBACK rawProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_CHAR)committed.push_back(static_cast<wchar_t>(wParam));
    return DefWindowProcW(window,message,wParam,lParam);
}
void observe() {
    if(!input)return;
    AcquireSRWLockShared(&input->imeLock);
    maximumImm=std::max(maximumImm,input->imeCandidateCount);
    ReleaseSRWLockShared(&input->imeLock);
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
        if(!splitRendering)draw();observe();Sleep(10);
    } while(GetTickCount64()<until);
}
}
int main(int argc,char** argv) {
    bool raw=false,render=false,stall=false;
    QString output;
    unsigned hold=0;
    for(int i=1;i<argc;++i) {
        const std::string_view option=argv[i];
        if(option=="--raw")raw=true;
        else if(option=="--render")render=raw=true;
        else if(option=="--split")splitRendering=render=raw=true;
        else if(option=="--stall")stall=true;
        else if(option=="--output"&&i+1<argc)output=QString::fromLocal8Bit(argv[++i]);
        else if(option=="--hold")hold=45000;
        else {std::printf("Unknown option: %s\n",argv[i]);return 2;}
    }
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const HWND previous=GetForegroundWindow();
    const HKL previousLayout=GetKeyboardLayout(0);
    auto state=std::make_unique<mcoverlay::OverlayInputState>();input=state.get();
    input->imeEnabled=true;
    WNDCLASSW rawClass{};rawClass.lpfnWndProc=rawProcedure;rawClass.style=CS_OWNDC;
    rawClass.hInstance=GetModuleHandleW(nullptr);rawClass.lpszClassName=L"ArcveilImeRawTest";
    RegisterClassW(&rawClass);
    HWND window=CreateWindowExW(0,raw?rawClass.lpszClassName:L"EDIT",L"",WS_OVERLAPPEDWINDOW|ES_MULTILINE,
        120,120,render?1100:600,render?600:300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 2;
    SetWindowTextW(window,raw?L"Arcveil v56.2 - live IME test":L"");
    if(!render)original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&procedure)));
    ShowWindow(window,SW_SHOW);
    // A runner may start us with SW_HIDE; Windows overrides the first show.
    if(!IsWindowVisible(window))ShowWindow(window,SW_SHOW);
    const DWORD foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
    const DWORD ownThread=GetCurrentThreadId();
    const bool attached=foregroundThread!=ownThread&&AttachThreadInput(ownThread,foregroundThread,TRUE);
    SetForegroundWindow(window);BringWindowToTop(window);SetFocus(window);
    if(attached)AttachThreadInput(ownThread,foregroundThread,FALSE);
    if(raw) {
        ImmAssociateContextEx(window,nullptr,IACE_DEFAULT);
        CreateCaret(window,nullptr,2,20);SetCaretPos(20,40);ShowCaret(window);
    }
    HGLRC context=nullptr;
    std::thread renderThread;
    if(render) {
        renderDc=GetDC(window);
        PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;
        format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;format.cDepthBits=24;
        const int pixelFormat=ChoosePixelFormat(renderDc,&format);
        if(!pixelFormat||!SetPixelFormat(renderDc,pixelFormat,&format))return 2;
        context=wglCreateContext(renderDc);
        if(!context)return 2;
        renderer=std::make_unique<mcoverlay::OverlayRenderer>();
        snapshot=std::make_unique<mcoverlay::GameSnapshot>();snapshot->gameScreenOpen=true;
        mcoverlay::FeatureSettings settings{};settings.fullscreenImeFixEnabled=true;
        renderer->setFeatureSettings(settings);
        if(splitRendering) {
            renderThread=std::thread([context] {
                if(wglMakeCurrent(renderDc,context)) {
                    draw();
                    if(renderer->initialized())renderInput=mcoverlay::OverlayRendererTestAccess::state(*renderer);
                    while(!renderStop) {draw();Sleep(10);}
                    renderer->shutdownWithCurrentContext();wglMakeCurrent(nullptr,nullptr);
                }
                renderDone=true;
            });
            const auto until=GetTickCount64()+3000U;
            while(!renderInput&&!renderDone&&GetTickCount64()<until)pump(10);
            input=renderInput.load();
        } else {
            if(!wglMakeCurrent(renderDc,context))return 2;
            draw();input=renderer->initialized()?mcoverlay::OverlayRendererTestAccess::state(*renderer):nullptr;
        }
        if(!input) {
            if(renderThread.joinable()) {
                renderStop=true;while(!renderDone)pump(10);renderThread.join();
            }
            return 2;
        }
    }
    std::printf("LIVE_IME window=%s render=%d split=%d\n",raw?"raw game-style HWND":"native EDIT",render?1:0,splitRendering?1:0);
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
    pump(400);
    if(render&&!output.isEmpty()) {
        QDir().mkpath(output);
        captureOutput=output;screenshotSaved=false;captureRequested=true;
        const auto until=GetTickCount64()+1000U;
        while(!captureDone&&GetTickCount64()<until)pump(10);
    }
    pump(hold);
    std::printf("LIVE_IME typed=nihao complete=%d overlayImm=%u overlayTsf=%u\n",
        typedAll?1:0,maximumImm,maximumTsf);
    std::printf("LIVE_IME candidateFrames=%u screenshotSaved=%d\n",candidateFrames.load(),screenshotSaved?1:0);
    bool passed=typedAll&&(maximumImm>0||maximumTsf>0)&&
        (!render||(candidateFrames>0&&screenshotSaved));
    if(stall) {
        const unsigned before=mcoverlay::log::nativeRestoreRequests;
        renderingPaused=true;pump(800);
        const bool restored=mcoverlay::log::nativeRestoreRequests>before;
        std::printf("LIVE_IME stalledRendererNativeRestoreRequested=%d\n",restored?1:0);
        passed=passed&&restored;renderingPaused=false;
    }
    if(GetForegroundWindow()==window&&GetFocus()==window) {
        INPUT events[2]{};events[0].type=events[1].type=INPUT_KEYBOARD;
        events[0].ki.wVk=events[1].ki.wVk=VK_SPACE;events[1].ki.dwFlags=KEYEVENTF_KEYUP;
        SendInput(2,events,sizeof(INPUT));pump(350);
        wchar_t editText[128]{};if(!raw)GetWindowTextW(window,editText,128);
        const std::wstring result=raw?committed:std::wstring(editText);
        const bool committedOk=result.find(L"你好")!=std::wstring::npos;
        const bool cleared=!input->tsf||!input->tsf->snapshot().active;
        std::printf("LIVE_IME committedNihao=%d candidatesCleared=%d\n",committedOk?1:0,cleared?1:0);
        passed=passed&&committedOk&&cleared;
    } else passed=false;
    input->imeEnabled=false;
    if(input->tsf)input->tsf->shutdownOnWindowThread();
    if(renderer) {
        input=nullptr;
        if(splitRendering) {
            renderStop=true;while(!renderDone)pump(10);renderThread.join();
        } else renderer->shutdownWithCurrentContext();
        renderer.reset();snapshot.reset();
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,renderDc);
    } else SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));
    if(raw)DestroyCaret();
    DestroyWindow(window);state.reset();input=nullptr;
    ActivateKeyboardLayout(previousLayout,0);
    if(profiles)profiles->Release();
    if(IsWindow(previous))SetForegroundWindow(previous);
    if(SUCCEEDED(com))CoUninitialize();
    return passed?0:1;
}
