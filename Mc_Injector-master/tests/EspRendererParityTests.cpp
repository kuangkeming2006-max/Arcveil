// Historical Player ESP regression: this executable is linked once with the
// verbatim pre-Force world renderer, and once with the current production one.
// Expectations come from the historical executable's real drawing output.
#include "overlay_renderer_internal.h"
#include <imgui_internal.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_win32.h>
#include <gl/GL.h>
#include <objbase.h>
#include <QCryptographicHash>
#include <QImage>
#include <QString>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mcoverlay {
struct OverlayRendererTestAccess {
    static bool initialize(OverlayRenderer& renderer, HWND window, HGLRC context) {
        return renderer.initialize(window, context);
    }
    static void world(OverlayRenderer& renderer, const GameSnapshot& snapshot,
                      const FeatureSettings& features) {
        ImGui::SetCurrentContext(renderer.m_imguiContext);
        renderer.m_features = features;
        renderer.m_nametagAnimations = {};
        OverlayRenderer::RenderFrameContext frame{
            snapshot, false, ImGui::GetIO(), 1.0F, 1.0F / 60.0F, false, 0.0F};
        renderer.renderWorldOverlay(frame);
    }
};
}

namespace {
constexpr int width = 640, height = 480;
int checks = 0, failures = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", label.c_str()); }
}

struct WglWindow {
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    bool open() {
        WNDCLASSW cls{};
        cls.style = CS_OWNDC;
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"ArcveilEspHistoricalParity";
        RegisterClassW(&cls);
        window = CreateWindowW(cls.lpszClassName, L"Arcveil ESP renderer parity",
            WS_POPUP, -30000, -30000, width, height, nullptr, nullptr,
            cls.hInstance, nullptr);
        if (!window) return false;
        ShowWindow(window, SW_SHOWNOACTIVATE);
        dc = GetDC(window);
        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format); format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32; format.cDepthBits = 24;
        const int pixelFormat = ChoosePixelFormat(dc, &format);
        if (!pixelFormat || !SetPixelFormat(dc, pixelFormat, &format)) return false;
        context = wglCreateContext(dc);
        return context && wglMakeCurrent(dc, context);
    }
    ~WglWindow() {
        wglMakeCurrent(nullptr, nullptr);
        if (context) wglDeleteContext(context);
        if (dc) ReleaseDC(window, dc);
        if (window) DestroyWindow(window);
    }
};

std::string sha256(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
        .toHex().toStdString();
}
template<class Value> void append(QByteArray& bytes, const Value& value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
struct FrameResult {
    int vertices = 0, indices = 0;
    std::size_t coloredPixels = 0;
    std::string geometryHash, pixelHash;
};

FrameResult draw(mcoverlay::OverlayRenderer& renderer,
                 const mcoverlay::GameSnapshot& snapshot,
                 const mcoverlay::FeatureSettings& features,
                 const std::filesystem::path& image = {}) {
    FrameResult result;
    // The same production world renderer and OpenGL backend as the injected
    // overlay are used. A fixed clock removes Nametag pulse timing noise.
    // Three submissions let the real dynamic font atlas finish uploading.
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
        glViewport(0, 0, width, height);
        glDrawBuffer(GL_BACK);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DeltaTime = 1.0F / 60.0F;
        io.MousePos = ImVec2(-10000.0F, -10000.0F);
        // ImGui 1.92 uses Time, rather than FrameCount, to reset its background
        // draw list. Advance within the three-frame sample, then replay the
        // same clock sequence for each case so pulse animation is identical.
        GImGui->Time = 5.0 + static_cast<double>(frameIndex) / 60.0;
        ImGui::NewFrame();
        mcoverlay::OverlayRendererTestAccess::world(renderer, snapshot, features);
        const ImDrawList* background = ImGui::GetBackgroundDrawList();
        if (frameIndex == 2) {
            result.vertices = background->VtxBuffer.Size;
            result.indices = background->IdxBuffer.Size;
            QByteArray geometry;
            for (const auto& vertex : background->VtxBuffer) {
                append(geometry, vertex.pos.x); append(geometry, vertex.pos.y);
                // Dynamic font-atlas storage may grow as new text is seen.
                // Its UV allocation is irrelevant to shape/color parity;
                // the complete submitted framebuffer is checked separately.
                append(geometry, vertex.col);
            }
            for (const auto& index : background->IdxBuffer) append(geometry, index);
            // Omit process-specific texture pointer values and callback addresses.
            for (const auto& command : background->CmdBuffer) {
                append(geometry, command.ClipRect.x); append(geometry, command.ClipRect.y);
                append(geometry, command.ClipRect.z); append(geometry, command.ClipRect.w);
                append(geometry, command.ElemCount); append(geometry, command.IdxOffset);
                append(geometry, command.VtxOffset);
            }
            result.geometryHash = sha256(geometry);
        }
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glFinish();
        if (frameIndex == 2) {
            std::vector<unsigned char> pixels(width * height * 4);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4)
                if (pixels[pixel] || pixels[pixel + 1] || pixels[pixel + 2])
                    ++result.coloredPixels;
            result.pixelHash = sha256(QByteArray(
                reinterpret_cast<const char*>(pixels.data()),
                static_cast<qsizetype>(pixels.size())));
            if (!image.empty()) {
                const QImage captured(pixels.data(), width, height,
                    width * 4, QImage::Format_RGBA8888);
                check(captured.mirrored().save(QString::fromStdWString(image.wstring())),
                      "save actual WGL framebuffer " + image.filename().string());
            }
        }
        check(glGetError() == GL_NO_ERROR, "production world/backend GL submission");
    }
    return result;
}

struct Fixture {
    std::string name;
    mcoverlay::GameSnapshot snapshot;
    bool unsafeNametagHealth = false;
};

mcoverlay::GameSnapshot baseSnapshot() {
    mcoverlay::GameSnapshot snapshot{};
    snapshot.state = mcoverlay::GameSnapshot::State::Ready;
    snapshot.entityId = 1;
    snapshot.worldGeneration = 11;
    snapshot.entitySampleGeneration = 33;
    snapshot.hypixelServer = true;
    snapshot.matchActive = true;
    snapshot.ownTeam = 'c';
    snapshot.entityRenderTick = 0.35;
    snapshot.camera.valid = true;
    snapshot.camera.viewport = {0, 0, width, height};
    for (int diagonal = 0; diagonal < 4; ++diagonal) {
        snapshot.camera.modelView[static_cast<std::size_t>(diagonal * 5)] = 1;
        snapshot.camera.projection[static_cast<std::size_t>(diagonal * 5)] = 1;
    }
    auto& marker = snapshot.entityMarkers[0];
    snapshot.entityMarkerCount = 1;
    marker.bounds = {-0.16, -0.35, -0.2, 0.16, 0.35, 0.2};
    marker.previousX = -0.05;
    marker.currentX = 0.05;
    marker.distance = 3;
    marker.entityId = 100;
    marker.health = 18;
    marker.maxHealth = 20;
    marker.player = true;
    marker.confirmedPlayer = true;
    marker.teamColor = '9';
    std::snprintf(marker.playerName.data(), marker.playerName.size(), "OriginalPlayer");
    std::snprintf(marker.displayName.data(), marker.displayName.size(), "Visible Player");
    std::snprintf(marker.uuid.data(), marker.uuid.size(), "11111111-1111-4111-8111-111111111111");
    return snapshot;
}

std::vector<Fixture> fixtures() {
    std::vector<Fixture> all;
    const auto add = [&](const char* name) -> Fixture& {
        all.push_back({name, baseSnapshot(), false});
        all.back().snapshot.entityMarkers[0].entityId =
            100 + static_cast<int>(all.size());
        return all.back();
    };
    add("confirmed_enemy");
    add("missing_tab").snapshot.entityMarkers[0].confirmedPlayer = false;
    auto& nick = add("nickname_uuid_mismatch").snapshot;
    nick.entityMarkers[0].confirmedPlayer = false;
    std::snprintf(nick.entityMarkers[0].playerName.data(), 17, "UnknownNickname");
    std::snprintf(nick.entityMarkers[0].displayName.data(), 65, "Nicked Alias");
    nick.playerCount = 1;
    std::snprintf(nick.players[0].name.data(), 17, "DifferentTabName");
    std::snprintf(nick.players[0].uuid.data(), 37,
                  "22222222-2222-4222-8222-222222222222");
    auto& minigame = add("unrecognized_minigame").snapshot;
    minigame.matchActive = false;
    minigame.entityMarkers[0].confirmedPlayer = false;
    add("same_team_armor").snapshot.entityMarkers[0].armorTeam = 'c';
    add("same_team_roster").snapshot.entityMarkers[0].teamColor = 'c';
    add("dead_marker").snapshot.entityMarkers[0].health = 0;
    add("negative_health").snapshot.entityMarkers[0].health = -2;
    auto& nan = add("nonfinite_health");
    nan.snapshot.entityMarkers[0].health = std::numeric_limits<float>::quiet_NaN();
    nan.unsafeNametagHealth = true;
    auto& self = add("self_marker").snapshot;
    self.entityMarkers[0].entityId = self.entityId;
    add("negative_id").snapshot.entityMarkers[0].entityId = -7;
    auto& zero = add("zero_width_bounds").snapshot.entityMarkers[0];
    zero.bounds.maxX = zero.bounds.minX;
    auto& inverted = add("inverted_bounds").snapshot.entityMarkers[0];
    std::swap(inverted.bounds.minX, inverted.bounds.maxX);
    auto& nonplayer = add("nonplayer_living").snapshot.entityMarkers[0];
    nonplayer.player = nonplayer.confirmedPlayer = false;
    auto& fireball = add("fireball").snapshot;
    fireball.entityMarkers[0].player = false;
    fireball.entityMarkers[0].fireball = true;
    fireball.integratedSinglePlayer = true;
    auto& distantMarker = add("far_player").snapshot.entityMarkers[0];
    distantMarker.distance = 90;
    return all;
}

struct DisplayMode { std::string name; mcoverlay::FeatureSettings features; };
std::vector<DisplayMode> displayModes() {
    mcoverlay::FeatureSettings features{};
    features.entityEspPlayersOnly = true;
    features.nametagEnabled = false;
    features.showTeammateArrows = false;
    features.bedEspEnabled = false;
    features.labelsEnabled = false;
    features.fireballEspEnabled = true;
    features.aimAssistEnabled = false;
    std::vector<DisplayMode> modes{{"boxes", features}};
    features.showTeammateBoxes = false;
    modes.push_back({"team_boxes_hidden", features});
    features.showTeammateArrows = true;
    modes.push_back({"team_arrows", features});
    features.showTeammateArrows = false;
    features.showTeammateBoxes = true;
    features.nametagEnabled = true;
    modes.push_back({"confirmed_nametags", features});
    features.nametagAlways = true;
    modes.push_back({"all_player_nametags", features});
    features.showTeammateNametags = false;
    modes.push_back({"team_nametags_hidden", features});
    features.nametagNearbyEnemiesOnly = true;
    modes.push_back({"nearby_enemy_nametags", features});
    features.nametagEnabled = false;
    features.entityEspPlayersOnly = false;
    modes.push_back({"all_entities", features});
    features.entityEspEnabled = false;
    features.nametagEnabled = true;
    features.showTeammateNametags = true;
    features.nametagNearbyEnemiesOnly = false;
    modes.push_back({"nametags_only", features});
    return modes;
}

std::map<std::string, std::string> readResults(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::map<std::string, std::string> results;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find('\t');
        if (separator != std::string::npos)
            results.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    return results;
}
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::puts("Usage: EspRendererParity <output-directory> [historical-output-directory]");
        return 2;
    }
    const std::filesystem::path output = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(output);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WglWindow wgl;
    if (!wgl.open()) { std::puts("WGL unavailable; parity NOT passed"); return 2; }
    std::ofstream metadata(output / "renderer.txt");
    metadata << "side=" << MC_ESP_PARITY_SIDE << "\npreForceCommit="
             << MC_ESP_PARITY_BASELINE_COMMIT << "\ncompiledWorldSha256="
             << MC_ESP_PARITY_WORLD_SHA256 << "\nGL_RENDERER="
             << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
             << "\nGL_VERSION=" << reinterpret_cast<const char*>(glGetString(GL_VERSION))
             << "\nThis is actual production WGL renderer output on synthetic snapshots; not Minecraft.\n";
    std::map<std::string, std::string> actual;
    {
        mcoverlay::OverlayRenderer renderer;
        if (!mcoverlay::OverlayRendererTestAccess::initialize(renderer, wgl.window, wgl.context)) {
            std::puts("Production renderer initialization failed; parity NOT passed");
            return 2;
        }
        const auto cases = fixtures();
        for (const auto& display : displayModes()) {
            std::string previousFilterDrawSet;
            for (int targetFilter = 0; targetFilter <= 1; ++targetFilter) {
                auto features = display.features;
                features.aimTargetFilter = targetFilter;
                std::string drawSet;
                for (const auto& fixture : cases) {
                    auto caseFeatures = features;
                    // The old renderer's NaN-health Nametag output is undefined;
                    // keep this adversary specific to its actual Hitbox output.
                    if (fixture.unsafeNametagHealth) caseFeatures.nametagEnabled = false;
                    auto boxes = caseFeatures;
                    boxes.nametagEnabled = false;
                    boxes.showTeammateArrows = false;
                    const auto hitbox = draw(renderer, fixture.snapshot, boxes);
                    const std::string key = display.name + "/" + fixture.name +
                        "/filter" + std::to_string(targetFilter);
                    const auto rendered = draw(renderer, fixture.snapshot, caseFeatures,
                        output / (display.name + "_" + fixture.name + "_" +
                                  std::to_string(targetFilter) + ".png"));
                    const auto& marker = fixture.snapshot.entityMarkers[0];
                    const bool drawnPlayerHitbox = marker.player && !marker.fireball &&
                        hitbox.vertices > 0 && hitbox.coloredPixels > 0;
                    if (drawnPlayerHitbox)
                        drawSet += fixture.name + ":" + std::to_string(marker.entityId) + ",";
                    std::ostringstream value;
                    value << marker.entityId << '\t' << hitbox.vertices << '\t'
                          << hitbox.coloredPixels << '\t' << rendered.vertices << '\t'
                          << rendered.indices << '\t' << rendered.coloredPixels << '\t'
                          << rendered.geometryHash << '\t' << rendered.pixelHash;
                    actual.emplace(key, value.str());
                    if (targetFilter == 1)
                        check(actual.at(display.name + "/" + fixture.name + "/filter0") ==
                              value.str(), key + " unchanged by Normal/Force selection");
                    // Independently establish that the historical oracle really
                    // draws adversarial markers that the v56.9.5 global guard hid.
                    if (display.name == "boxes" && marker.player)
                        check(drawnPlayerHitbox, key + " old renderer draws supplied projectable player");
                }
                actual.emplace(display.name + "/drawn_player_hitbox_ids/filter" +
                               std::to_string(targetFilter), drawSet);
                if (targetFilter == 1)
                    check(previousFilterDrawSet == drawSet,
                          display.name + " exact Hitbox ID set invariant across filter");
                previousFilterDrawSet = drawSet;

                // A complete shared snapshot additionally checks draw ordering,
                // overlapping Nametags and mixed player/nonplayer behaviour.
                auto mixed = baseSnapshot();
                mixed.entityMarkerCount = 0;
                for (const auto& fixture : cases) {
                    if (fixture.unsafeNametagHealth) continue;
                    auto marker = fixture.snapshot.entityMarkers[0];
                    const auto index = mixed.entityMarkerCount++;
                    const double x = -0.75 + static_cast<double>(index % 5) * 0.37;
                    const double y = -0.62 + static_cast<double>(index / 5) * 0.59;
                    marker.bounds = {x - 0.055, y - 0.16, -0.1,
                                     x + 0.055, y + 0.16, 0.1};
                    marker.currentX = marker.previousX = x;
                    marker.currentY = marker.previousY = y;
                    mixed.entityMarkers[index] = marker;
                }
                const std::string key = display.name + "/mixed/filter" + std::to_string(targetFilter);
                const auto result = draw(renderer, mixed, features,
                    output / (display.name + "_mixed_" + std::to_string(targetFilter) + ".png"));
                std::ostringstream value;
                value << result.vertices << '\t' << result.indices << '\t' << result.coloredPixels
                      << '\t' << result.geometryHash << '\t' << result.pixelHash;
                actual.emplace(key, value.str());
                if (targetFilter == 1)
                    check(actual.at(display.name + "/mixed/filter0") == value.str(),
                          key + " same shared snapshot geometry and framebuffer");
            }
        }
        renderer.shutdownWithCurrentContext();
        check(!renderer.initialized(), "production renderer shutdown");
    }
    std::ofstream manifest(output / "results.tsv");
    manifest << "# key\tid\tboxVertices\tboxPixels\tvertices\tindices\tpixels\tgeometrySHA256\tpixelSHA256\n";
    for (const auto& [key, value] : actual) manifest << key << '\t' << value << '\n';
    manifest.close();
    if (argc == 3) {
        const auto historical = readResults(std::filesystem::path(argv[2]) / "results.tsv");
        check(!historical.empty(), "historical executable produced actual draw output");
        check(actual.size() == historical.size(), "historical/current scenario cardinality");
        for (const auto& [key, value] : actual) {
            const auto baseline = historical.find(key);
            check(baseline != historical.end() && baseline->second == value,
                  "pre-Force 863b92b vs repaired actual renderer: " + key);
        }
    }
    std::ofstream summary(output / "summary.txt");
    summary << MC_ESP_PARITY_SIDE << ": " << checks << " checks, " << failures
            << " failures; " << actual.size() << " recorded draw observations\n";
    std::printf("%s: %d actual WGL renderer checks, %d failures; %zu observations\n",
                MC_ESP_PARITY_SIDE, checks, failures, actual.size());
    CoUninitialize();
    return failures ? 1 : 0;
}
