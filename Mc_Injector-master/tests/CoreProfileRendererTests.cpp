#include "overlay_renderer.h"
#include <imgui.h>
#include <gl/GL.h>
#include <cstdio>
#include <objbase.h>

namespace mcoverlay {
struct OverlayRendererTestAccess {
    static bool core(const OverlayRenderer& renderer) {return renderer.m_coreProfile;}
};
}
// Real WGL 3.2 core context + production renderer. No Minecraft JVM or shader mods.
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"ArcveilCoreProfileTest";cls.style=CS_OWNDC;
    RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"Arcveil core renderer test",WS_OVERLAPPEDWINDOW,
        -30000,-30000,1280,800,nullptr,nullptr,cls.hInstance,nullptr);
    if (!window) return 2;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;
    format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;format.cDepthBits=24;
    const int pixel=ChoosePixelFormat(dc,&format);
    if (!pixel || !SetPixelFormat(dc,pixel,&format)) return 2;
    HGLRC bootstrap=wglCreateContext(dc);
    if (!bootstrap || !wglMakeCurrent(dc,bootstrap)) return 2;
    const auto create=reinterpret_cast<HGLRC(WINAPI*)(HDC,HGLRC,const int*)>(wglGetProcAddress("wglCreateContextAttribsARB"));
    const int attributes[]={0x2091,3,0x2092,2,0x9126,1,0};
    HGLRC core=create?create(dc,nullptr,attributes):nullptr;
    if (!core) {std::puts("WGL 3.2 core context unavailable; test NOT passed");return 2;}
    wglMakeCurrent(nullptr,nullptr);wglDeleteContext(bootstrap);
    if (!wglMakeCurrent(dc,core)) return 2;
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* label) {++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}};
    {
        mcoverlay::OverlayRenderer renderer;
        mcoverlay::GameSnapshot snapshot{};
        snapshot.state=mcoverlay::GameSnapshot::State::Ready;
        bool initialized=false,drawn=false;
        for (unsigned frame=0;frame<8;++frame) {
            glViewport(0,0,1280,800);glClearColor(0.1f,0.2f,0.3f,1);glClear(GL_COLOR_BUFFER_BIT);
            initialized|=renderer.render(dc,snapshot,true);
            auto data=ImGui::GetDrawData();drawn|=data && data->TotalVtxCount>0;
            check(glGetError()==GL_NO_ERROR,"core frame has no removed GL calls/state errors");
            SwapBuffers(dc);
        }
        check(initialized && renderer.initialized(),"production renderer initialized on core context");
        check(mcoverlay::OverlayRendererTestAccess::core(renderer),"OpenGL3 backend selected from actual context");
        check(drawn,"production ImGui geometry submitted");
        renderer.shutdownWithCurrentContext();
        check(!renderer.initialized(),"core backend tears down on owning context");
        check(glGetError()==GL_NO_ERROR,"core teardown has no GL error");
    }
    wglMakeCurrent(nullptr,nullptr);wglDeleteContext(core);ReleaseDC(window,dc);DestroyWindow(window);
    CoUninitialize();
    std::printf("%d real WGL core checks, %d failures (not Minecraft)\n",checks,failures);
    return failures?1:0;
}
