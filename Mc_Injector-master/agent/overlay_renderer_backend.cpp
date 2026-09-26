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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "bed_png.h"
#include "block_textures.h"

namespace mcoverlay {

namespace renderer_detail {

bool rawModuleResource(HMODULE module, const int identifier,
                       const unsigned char*& data,
                       std::size_t& size) noexcept
{
    data = nullptr;
    size = 0U;
    if (!module) return false;
    const HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(identifier),
                                           RT_RCDATA);
    if (!resource) return false;
    const HGLOBAL loaded = ::LoadResource(module, resource);
    if (!loaded) return false;
    const DWORD bytes = ::SizeofResource(module, resource);
    const void* const pointer = ::LockResource(loaded);
    if (!pointer || bytes == 0U) return false;
    data = static_cast<const unsigned char*>(pointer);
    size = static_cast<std::size_t>(bytes);
    return true;
}

} // namespace renderer_detail

using namespace renderer_detail;

OverlayRenderer::OverlayRenderer()
    : m_inputState(new (std::nothrow) OverlayInputState)
{
}

OverlayRenderer::~OverlayRenderer()
{
    abandonAfterHookDisabled();
    delete m_inputState;
    m_inputState = nullptr;
}

bool OverlayRenderer::ownsCurrentContext() const noexcept
{
    return m_initialized && m_glContext != nullptr &&
           m_glContext == ::wglGetCurrentContext();
}

void OverlayRenderer::applyGuiScaleStyle(const float scale, const int fontIndex) noexcept
{
    ImGuiStyle fresh{};
    ImGui::StyleColorsDark(&fresh);
    fresh.WindowRounding = 16.0F * scale;
    fresh.ChildRounding = 12.0F * scale;
    fresh.FrameRounding = 9.0F * scale;
    fresh.PopupRounding = 12.0F * scale;
    fresh.WindowPadding = ImVec2(17.0F * scale, 15.0F * scale);
    fresh.FramePadding = ImVec2(10.0F * scale, 5.0F * scale);
    fresh.ItemSpacing = ImVec2(10.0F * scale, 8.0F * scale);
    fresh.ItemInnerSpacing = ImVec2(7.0F * scale, 5.0F * scale);
    fresh.WindowBorderSize = 0.0F;
    fresh.ChildBorderSize = 0.0F;
    fresh.PopupBorderSize = 0.0F;
    fresh.FrameBorderSize = 0.0F;
    fresh.ScrollbarRounding = 12.0F * scale;
    fresh.GrabRounding = 9.0F * scale;
    fresh.Colors[ImGuiCol_WindowBg] = ImVec4(0.055F, 0.050F, 0.072F, 0.88F);
    fresh.Colors[ImGuiCol_ChildBg] = ImVec4(0.090F, 0.082F, 0.112F, 0.82F);
    fresh.Colors[ImGuiCol_PopupBg] = ImVec4(0.075F, 0.068F, 0.095F, 0.96F);
    fresh.Colors[ImGuiCol_Header] = ImVec4(0.39F, 0.31F, 0.68F, 0.85F);
    fresh.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.50F, 0.39F, 0.82F, 0.90F);
    fresh.Colors[ImGuiCol_Button] = ImVec4(0.42F, 0.33F, 0.75F, 0.92F);
    fresh.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.52F, 0.41F, 0.88F, 1.0F);
    fresh.Colors[ImGuiCol_ButtonActive] = ImVec4(0.61F, 0.48F, 0.96F, 1.0F);
    fresh.Colors[ImGuiCol_FrameBg] = ImVec4(0.15F, 0.14F, 0.19F, 0.90F);
    fresh.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21F, 0.19F, 0.28F, 0.96F);
    fresh.Colors[ImGuiCol_Separator] = ImVec4(0.40F, 0.36F, 0.48F, 0.36F);
    ImGui::GetStyle() = fresh;
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.0F;
    const int selectedFont = std::clamp(fontIndex, 0, 3);
    if (m_fonts[static_cast<std::size_t>(selectedFont)] != nullptr) {
        io.FontDefault = m_fonts[static_cast<std::size_t>(selectedFont)];
    }
    m_appliedGuiScaleIndex = selectedFont;
}

bool OverlayRenderer::initialize(HWND const window, HGLRC const context) noexcept
{
    if (m_permanentlyDisabled) {
        return false;
    }
    if (m_inputState == nullptr) {
        m_inputState = new (std::nothrow) OverlayInputState;
        if (m_inputState == nullptr) {
            return false;
        }
    }

    IMGUI_CHECKVERSION();
    m_imguiContext = ImGui::CreateContext();
    if (m_imguiContext == nullptr) {
        return false;
    }
    ImGui::SetCurrentContext(m_imguiContext);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // FontGlobalScale enlarges a single bitmap and becomes visibly blurry.
    // Build four independently rasterized Segoe UI sizes into one atlas and
    // switch the default font only at the safe pre-NewFrame boundary.
    char windowsDirectory[MAX_PATH]{};
    const UINT windowsLength = ::GetWindowsDirectoryA(windowsDirectory, MAX_PATH);
    std::array<char, MAX_PATH> fontPath{};
    std::array<char, MAX_PATH> boldFontPath{};
    if (windowsLength > 0U && windowsLength + 20U < fontPath.size()) {
        std::snprintf(fontPath.data(), fontPath.size(), "%s\\Fonts\\segoeui.ttf",
                      windowsDirectory);
        std::snprintf(boldFontPath.data(), boldFontPath.size(),
                      "%s\\Fonts\\segoeuib.ttf", windowsDirectory);
    }
    constexpr std::array<float, 4U> fontSizes{15.0F, 19.0F, 23.0F, 27.0F};
    for (std::size_t index = 0U; index < m_fonts.size(); ++index) {
        ImFontConfig fontConfig{};
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;
        if (fontPath[0U] != '\0') {
            m_fonts[index] = io.Fonts->AddFontFromFileTTF(
                fontPath.data(), fontSizes[index], &fontConfig,
                io.Fonts->GetGlyphRangesDefault());
        }
        if (m_fonts[index] == nullptr) {
            fontConfig.SizePixels = fontSizes[index];
            m_fonts[index] = io.Fonts->AddFontDefault(&fontConfig);
        }
        ImFontConfig boldConfig{};
        boldConfig.OversampleH = 3;
        boldConfig.OversampleV = 2;
        boldConfig.PixelSnapH = false;
        if (boldFontPath[0U] != '\0') {
            m_boldFonts[index] = io.Fonts->AddFontFromFileTTF(
                boldFontPath.data(), fontSizes[index], &boldConfig,
                io.Fonts->GetGlyphRangesDefault());
        }
        if (m_boldFonts[index] == nullptr) m_boldFonts[index] = m_fonts[index];
    }
    // One dedicated CJK font is enough for the transient IME card. Building
    // four full CJK atlases would waste substantial memory inside the game.
    std::array<char, MAX_PATH> imeFontPath{};
    if (windowsLength > 0U && windowsLength + 18U < imeFontPath.size()) {
        std::snprintf(imeFontPath.data(), imeFontPath.size(),
                      "%s\\Fonts\\msyh.ttc", windowsDirectory);
        if (::GetFileAttributesA(imeFontPath.data()) == INVALID_FILE_ATTRIBUTES) {
            std::snprintf(imeFontPath.data(), imeFontPath.size(),
                          "%s\\Fonts\\msjh.ttc", windowsDirectory);
        }
    }
    if (imeFontPath[0U] != '\0' &&
        ::GetFileAttributesA(imeFontPath.data()) != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig imeConfig{};
        imeConfig.OversampleH = 2;
        imeConfig.OversampleV = 2;
        imeConfig.PixelSnapH = false;
        m_imeFont = io.Fonts->AddFontFromFileTTF(
            imeFontPath.data(), 22.0F, &imeConfig,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }

    // GSMTC strings are UTF-8 all the way through the controller protocol;
    // the old squares/question marks were caused by the Latin-only Segoe UI
    // atlas. Keep one media-only CJK face and merge Japanese kana/kanji into
    // it. The GUI continues to use the four compact Latin atlases above.
    std::array<char, MAX_PATH> mediaChinesePath{};
    std::array<char, MAX_PATH> mediaJapanesePath{};
    if (windowsLength > 0U && windowsLength + 24U < mediaChinesePath.size()) {
        std::snprintf(mediaChinesePath.data(), mediaChinesePath.size(),
                      "%s\\Fonts\\msyh.ttc", windowsDirectory);
        std::snprintf(mediaJapanesePath.data(), mediaJapanesePath.size(),
                      "%s\\Fonts\\YuGothM.ttc", windowsDirectory);
        if (::GetFileAttributesA(mediaJapanesePath.data()) == INVALID_FILE_ATTRIBUTES) {
            std::snprintf(mediaJapanesePath.data(), mediaJapanesePath.size(),
                          "%s\\Fonts\\msgothic.ttc", windowsDirectory);
        }
    }
    if (mediaChinesePath[0U] != '\0' &&
        ::GetFileAttributesA(mediaChinesePath.data()) != INVALID_FILE_ATTRIBUTES) {
        ImFontConfig mediaConfig{};
        // Small HUD text benefits noticeably from horizontal oversampling and
        // a slight coverage lift.  Keep vertical oversampling at one (ImGui
        // does not use sub-pixel Y placement) so the full CJK atlas stays
        // reasonably small inside the target JVM.
        mediaConfig.OversampleH = 2;
        mediaConfig.OversampleV = 1;
        mediaConfig.PixelSnapH = false;
        mediaConfig.RasterizerMultiply = 1.12F;
        m_mediaFont = io.Fonts->AddFontFromFileTTF(
            mediaChinesePath.data(), 20.0F, &mediaConfig,
            io.Fonts->GetGlyphRangesChineseFull());
        if (m_mediaFont != nullptr && mediaJapanesePath[0U] != '\0' &&
            ::GetFileAttributesA(mediaJapanesePath.data()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig japaneseConfig{};
            japaneseConfig.MergeMode = true;
            japaneseConfig.DstFont = m_mediaFont;
            japaneseConfig.OversampleH = 2;
            japaneseConfig.OversampleV = 1;
            japaneseConfig.PixelSnapH = false;
            japaneseConfig.RasterizerMultiply = 1.12F;
            (void)io.Fonts->AddFontFromFileTTF(
                mediaJapanesePath.data(), 20.0F, &japaneseConfig,
                io.Fonts->GetGlyphRangesJapanese());
        }
    }

    m_appliedGuiScaleIndex = -1;
    m_animatedGuiScale = guiScaleForIndex(m_guiScaleIndex);
    applyGuiScaleStyle(m_animatedGuiScale, m_guiScaleIndex);

    if (!ImGui_ImplWin32_Init(window)) {
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
        m_fonts = {};
        m_boldFonts = {};
        m_imeFont = nullptr;
        m_mediaFont = nullptr;
        return false;
    }
    if (!ImGui_ImplOpenGL2_Init()) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(m_imguiContext);
        m_imguiContext = nullptr;
        m_fonts = {};
        m_boldFonts = {};
        m_imeFont = nullptr;
        m_mediaFont = nullptr;
        return false;
    }

    m_inputState->imguiContext.store(m_imguiContext, std::memory_order_release);
    m_inputState->window.store(window, std::memory_order_release);
    updateImeState(*m_inputState, window, ImeMessageAction::ResetLayout,
        reinterpret_cast<LPARAM>(::GetKeyboardLayout(
            ::GetWindowThreadProcessId(window, nullptr))));
    m_inputState->acceptImGuiMessages.store(true, std::memory_order_release);
    m_inputState->fallbackPrimed = false;
    DWORD windowProcessId = 0U;
    const DWORD windowThreadId = ::GetWindowThreadProcessId(window, &windowProcessId);
    m_inputState->directImGuiWndProc.store(
        windowThreadId != 0U && windowThreadId == ::GetCurrentThreadId(),
        std::memory_order_release);
    // Minecraft/LWJGL remains the cursor style owner. The overlay only keeps
    // the cursor visible while interactive; it never swaps the game's native
    // pointer for ImGui's smaller default arrow or resize sprites.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    const bool windowProcedureInstalled = m_windowProcedure.install(
        window, &OverlayRenderer::handleWindowMessage, m_inputState);
    m_inputState->windowProcedureAvailable.store(
        windowProcedureInstalled, std::memory_order_release);
    if (!windowProcedureInstalled && !m_wndProcFallbackLogged) {
        log::info("WndProc input hook unavailable; using non-blocking async input fallback.");
        m_wndProcFallbackLogged = true;
    }

    int bedWidth = 0, bedHeight = 0, bedChannels = 0;
    unsigned char* bedPixels = stbi_load_from_memory(BED_PNG_DATA, sizeof(BED_PNG_DATA), &bedWidth, &bedHeight, &bedChannels, 4);
    if (bedPixels) {
        GLint lastTexture = 0;
        ::glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
        ::glGenTextures(1, &m_bedTexture);
        ::glBindTexture(GL_TEXTURE_2D, m_bedTexture);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bedWidth, bedHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, bedPixels);
        stbi_image_free(bedPixels);

        auto loadBlockTexture = [](const unsigned char* data, std::size_t size) -> unsigned {
            int w = 0, h = 0, c = 0;
            unsigned char* pixels = stbi_load_from_memory(data, size, &w, &h, &c, 4);
            if (!pixels) return 0U;
            unsigned tex = 0U;
            ::glGenTextures(1, &tex);
            ::glBindTexture(GL_TEXTURE_2D, tex);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            stbi_image_free(pixels);
            return tex;
        };
        m_blockTextures[0] = loadBlockTexture(BLOCK_WOOL_PNG, sizeof(BLOCK_WOOL_PNG));
        m_blockTextures[1] = loadBlockTexture(BLOCK_PLANKS_OAK_PNG, sizeof(BLOCK_PLANKS_OAK_PNG));
        m_blockTextures[2] = loadBlockTexture(BLOCK_HARDENED_CLAY_PNG, sizeof(BLOCK_HARDENED_CLAY_PNG));
        m_blockTextures[3] = loadBlockTexture(BLOCK_GLASS_PNG, sizeof(BLOCK_GLASS_PNG));
        m_blockTextures[4] = loadBlockTexture(BLOCK_END_STONE_PNG, sizeof(BLOCK_END_STONE_PNG));
        m_blockTextures[5] = loadBlockTexture(BLOCK_OBSIDIAN_PNG, sizeof(BLOCK_OBSIDIAN_PNG));

        ::glBindTexture(GL_TEXTURE_2D, lastTexture);
    }

    // Kenney's CC0 Input Prompts atlas is embedded in the DLL so an injected
    // agent never depends on Minecraft's working directory or loose files.
    HMODULE agentModule = nullptr;
    if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&OverlayRenderer::handleWindowMessage),
            &agentModule)) {
        const unsigned char* atlasBytes = nullptr;
        std::size_t atlasByteCount = 0U;
        const unsigned char* atlasXml = nullptr;
        std::size_t atlasXmlBytes = 0U;
        if (rawModuleResource(agentModule, MCOVERLAY_KENNEY_INPUT_PROMPTS_XML,
                              atlasXml, atlasXmlBytes)) {
            m_mediaKeycapAtlasXml = reinterpret_cast<const char*>(atlasXml);
            m_mediaKeycapAtlasXmlSize = atlasXmlBytes;
        }
        if (rawModuleResource(agentModule, MCOVERLAY_KENNEY_INPUT_PROMPTS_PNG,
                              atlasBytes, atlasByteCount)) {
            int atlasWidth=0,atlasHeight=0,channels=0;
            unsigned char* atlasPixels=stbi_load_from_memory(atlasBytes,
                static_cast<int>(atlasByteCount),&atlasWidth,&atlasHeight,&channels,4);
            if (atlasPixels && atlasWidth>0 && atlasHeight>0) {
                GLint previousTexture=0;
                ::glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);
                ::glGenTextures(1,&m_mediaKeycapTexture);
                ::glBindTexture(GL_TEXTURE_2D,m_mediaKeycapTexture);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
                ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
                ::glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,atlasWidth,atlasHeight,0,
                               GL_RGBA,GL_UNSIGNED_BYTE,atlasPixels);
                ::glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(previousTexture));
            }
            stbi_image_free(atlasPixels);
        }
    }

    m_window = window;
    m_glContext = context;
    m_initialized = true;
    return true;
}

void OverlayRenderer::shutdownWithCurrentContext() noexcept
{
    if (!m_initialized || m_imguiContext == nullptr) {
        return;
    }
    stopTsf(m_window, m_inputState);
    m_gaussianBlur.release();
    if (m_inputState != nullptr) {
        m_inputState->acceptImGuiMessages.store(false, std::memory_order_release);
        m_inputState->interactive.store(false, std::memory_order_release);
        m_inputState->windowProcedureAvailable.store(false, std::memory_order_release);
        m_inputState->directImGuiWndProc.store(false, std::memory_order_release);
        m_inputState->captureHotkey.store(false, std::memory_order_release);
        m_inputState->window.store(nullptr, std::memory_order_release);
    }
    if (!m_windowProcedure.restore()) {
        abandonAfterWndProcDrainTimeout();
        return;
    }
    if (m_inputState != nullptr) {
        m_inputState->imguiContext.store(nullptr, std::memory_order_release);
    }
    ImGui::SetCurrentContext(m_imguiContext);
    if (m_blurTexture != 0U) {
        const GLuint texture = static_cast<GLuint>(m_blurTexture);
        ::glDeleteTextures(1, &texture);
        m_blurTexture = 0U;
        m_blurWidth = 0;
        m_blurHeight = 0;
    }
    if (m_bedTexture != 0U) {
        const GLuint texture = static_cast<GLuint>(m_bedTexture);
        ::glDeleteTextures(1, &texture);
        m_bedTexture = 0U;
    }
    if (m_mediaCoverTexture != 0U) {
        const GLuint texture = static_cast<GLuint>(m_mediaCoverTexture);
        ::glDeleteTextures(1, &texture);
        m_mediaCoverTexture = 0U;
    }
    if(m_mediaPreviousCoverTexture!=0U) {
        const GLuint texture=static_cast<GLuint>(m_mediaPreviousCoverTexture);
        ::glDeleteTextures(1,&texture);
        m_mediaPreviousCoverTexture=0U;
    }
    if(m_mediaKeycapTexture!=0U) {
        const GLuint texture=static_cast<GLuint>(m_mediaKeycapTexture);
        ::glDeleteTextures(1,&texture);
        m_mediaKeycapTexture=0U;
    }
    for (unsigned& tex : m_blockTextures) {
        if (tex != 0U) {
            const GLuint t = static_cast<GLuint>(tex);
            ::glDeleteTextures(1, &t);
            tex = 0U;
        }
    }
    for (BlacklistTexture& cached : m_blacklistTextures) {
        if (cached.texture != 0U) {
            const GLuint texture = static_cast<GLuint>(cached.texture);
            ::glDeleteTextures(1, &texture);
        }
        cached = {};
    }
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(m_imguiContext);
    m_imguiContext = nullptr;
    m_fonts = {};
    m_boldFonts = {};
    m_imeFont = nullptr;
    m_mediaFont = nullptr;
    m_blurTexture = 0U;
    m_bedTexture = 0U;
    m_mediaCoverTexture = 0U;
    m_mediaPreviousCoverTexture = 0U;
    m_mediaKeycapTexture = 0U;
    m_mediaKeycapAtlasXml = nullptr;
    m_mediaKeycapAtlasXmlSize = 0U;
    m_mediaLoadedCoverPath = {};
    m_mediaLoadedTitle = {};
    m_mediaLoadedArtist = {};
    m_mediaPreviousTitle = {};
    m_mediaPreviousArtist = {};
    m_blurWidth = 0;
    m_blurHeight = 0;
    m_blacklistTextures = {};
    m_cursorSessionActive = false;
    m_window = nullptr;
    m_glContext = nullptr;
    m_initialized = false;
}

void OverlayRenderer::abandonForContextChange() noexcept
{
    stopTsf(m_window, m_inputState);
    m_gaussianBlur.abandon();
    if (m_imguiContext != nullptr) {
        if (m_inputState != nullptr) {
            m_inputState->acceptImGuiMessages.store(false, std::memory_order_release);
            m_inputState->interactive.store(false, std::memory_order_release);
            m_inputState->windowProcedureAvailable.store(false, std::memory_order_release);
            m_inputState->directImGuiWndProc.store(false, std::memory_order_release);
            m_inputState->captureHotkey.store(false, std::memory_order_release);
            m_inputState->window.store(nullptr, std::memory_order_release);
        }
        if (!m_windowProcedure.restore()) {
            abandonAfterWndProcDrainTimeout();
            return;
        }
        if (m_inputState != nullptr) {
            m_inputState->imguiContext.store(nullptr, std::memory_order_release);
        }
        // The old HGLRC is not current. Neither ImGui_ImplOpenGL2_Shutdown nor
        // DestroyContext is legal here: the former would delete driver objects
        // in the new context, while the latter asserts because renderer backend
        // data is still attached. Retain this small, unreachable generation;
        // initialize() creates a fresh context for the new HGLRC.
        log::error("Retaining an ImGui generation whose OpenGL context is no longer current.");
    }
    m_imguiContext = nullptr;
    m_fonts = {};
    m_boldFonts = {};
    m_imeFont = nullptr;
    m_mediaFont = nullptr;
    // The old HGLRC is unavailable, so its texture cannot be deleted here.
    // Drop the name to prevent a later context generation from deleting an
    // unrelated object which happens to reuse the same GLuint value.
    m_blurTexture = 0U;
    m_bedTexture = 0U;
    m_mediaCoverTexture = 0U;
    m_mediaPreviousCoverTexture = 0U;
    m_mediaKeycapTexture = 0U;
    m_mediaKeycapAtlasXml = nullptr;
    m_mediaKeycapAtlasXmlSize = 0U;
    m_mediaLoadedCoverPath = {};
    m_mediaLoadedTitle = {};
    m_mediaLoadedArtist = {};
    m_mediaPreviousTitle = {};
    m_mediaPreviousArtist = {};
    m_blurWidth = 0;
    m_blurHeight = 0;
    m_blacklistTextures = {};
    m_cursorSessionActive = false;
    m_window = nullptr;
    m_glContext = nullptr;
    m_initialized = false;
}

void OverlayRenderer::abandonAfterWndProcDrainTimeout() noexcept
{
    // A callback selected the old handler before restore() unpublished it and
    // did not finish within the bounded drain. Destroying the ImGui context or
    // input bridge would race that callback. Both are intentionally retained;
    // no new handler can acquire them because WndProcHook is already inert.
    log::error("Retaining ImGui state after a WndProc drain timeout.");
    m_inputState = nullptr;
    m_imguiContext = nullptr;
    m_fonts = {};
    m_boldFonts = {};
    m_imeFont = nullptr;
    m_mediaFont = nullptr;
    m_blacklistTextures = {};
    m_mediaCoverTexture = 0U;
    m_mediaPreviousCoverTexture = 0U;
    m_mediaKeycapTexture = 0U;
    m_mediaKeycapAtlasXml = nullptr;
    m_mediaKeycapAtlasXmlSize = 0U;
    m_mediaLoadedCoverPath = {};
    m_mediaLoadedTitle = {};
    m_mediaLoadedArtist = {};
    m_mediaPreviousTitle = {};
    m_mediaPreviousArtist = {};
    m_cursorSessionActive = false;
    m_window = nullptr;
    m_glContext = nullptr;
    m_initialized = false;
    m_permanentlyDisabled = true;
}

void OverlayRenderer::abandonAfterHookDisabled() noexcept
{
    abandonForContextChange();
}

void OverlayRenderer::captureBackdropTexture() noexcept
{
    const ImGuiIO& io = ImGui::GetIO();
    const int width = static_cast<int>(io.DisplaySize.x);
    const int height = static_cast<int>(io.DisplaySize.y);
    if (width < 2 || height < 2 || m_backdropCapturedThisFrame) return;

    ::glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (m_blurTexture == 0U) {
        GLuint texture = 0U;
        ::glGenTextures(1, &texture);
        m_blurTexture = texture;
    }
    ::glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_blurTexture));
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    if (width != m_blurWidth || height != m_blurHeight) {
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                       GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        m_blurWidth = width;
        m_blurHeight = height;
    }
    ::glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    ::glPopAttrib();
    m_backdropCapturedThisFrame = true;
}

void OverlayRenderer::renderInventoryBlur(const float strength) noexcept
{
    if (strength <= 0.01F || m_features.clickGuiBlur <= 0) return;
    const ImGuiIO& io = ImGui::GetIO();
    captureBackdropTexture();
    const float amount = static_cast<float>(m_features.clickGuiBlur) / 100.0F;
    (void)m_gaussianBlur.draw(m_blurTexture,
        static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y),
        0.5F + 5.5F * amount, strength);
}


} // namespace mcoverlay
