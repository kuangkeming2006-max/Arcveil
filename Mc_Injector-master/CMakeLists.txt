cmake_minimum_required(VERSION 3.24)

project(MinecraftOverlayManager VERSION 1.0.0 LANGUAGES C CXX)

find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Quick3D
    Network
    Concurrent
)

qt_standard_project_setup()

option(MC_OVERLAY_CONSOLE "Keep a console for diagnostics and CI smoke tests" OFF)
option(MC_OVERLAY_UI_TESTS "Include isolated Qt input regression tests" OFF)

qt_add_executable(MinecraftOverlayManager
    src/main.cpp
    src/StartupLoader.h
    src/StartupLoader.cpp
    src/ApplicationLifecycle.h
    src/ApplicationLifecycle.cpp
    src/ProcessScanner.h
    src/ProcessScanner.cpp
    src/OverlayManager.h
    src/OverlayManager.cpp
    src/HypixelApiClient.h
    src/HypixelApiClient.cpp
    src/PlayerStatsService.h
    src/PlayerStatsService.cpp
    src/ApiKeyStore.h
    src/ApiKeyStore.cpp
    src/AppSettings.h
    src/AppSettings.cpp
    src/HotkeyCaptureService.h
    src/HotkeyCaptureService.cpp
    src/BlacklistService.h
    src/BlacklistService.cpp
    src/SkinProfileService.h
    src/SkinProfileService.cpp
    src/SkinCuboidGeometry.h
    src/SkinCuboidGeometry.cpp
    src/MediaSessionService.h
    src/MediaSessionService.cpp
)

if(WIN32)
    set(MC_MEDIA_HELPER "${CMAKE_CURRENT_BINARY_DIR}/tools/WindowsMediaBridge.exe")
    set(MC_CSC "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
    set(MC_FRAMEWORK64 "C:/Windows/Microsoft.NET/Framework64/v4.0.30319")
    file(GLOB MC_WINDOWS_WINMD
        "C:/Program Files (x86)/Windows Kits/10/UnionMetadata/10.*/Windows.winmd")
    list(SORT MC_WINDOWS_WINMD COMPARE NATURAL ORDER DESCENDING)
    list(GET MC_WINDOWS_WINMD 0 MC_WINDOWS_WINMD_LATEST)
    # Legacy .NET Framework csc treats an unquoted forward-slash absolute
    # source path as a compiler switch.  Feed it native Windows paths so the
    # helper is reproducible from Ninja as well as Visual Studio generators.
    file(TO_NATIVE_PATH "${MC_MEDIA_HELPER}" MC_MEDIA_HELPER_NATIVE)
    file(TO_NATIVE_PATH "${MC_FRAMEWORK64}" MC_FRAMEWORK64_NATIVE)
    file(TO_NATIVE_PATH "${MC_WINDOWS_WINMD_LATEST}" MC_WINDOWS_WINMD_NATIVE)
    file(TO_NATIVE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/media-helper/WindowsMediaBridge.cs"
        MC_MEDIA_HELPER_SOURCE_NATIVE)
    add_custom_command(OUTPUT "${MC_MEDIA_HELPER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/tools"
        COMMAND "${MC_CSC}" /nologo /target:exe /optimize+
            "/out:${MC_MEDIA_HELPER_NATIVE}"
            "/reference:${MC_FRAMEWORK64_NATIVE}\\System.Runtime.dll"
            "/reference:${MC_FRAMEWORK64_NATIVE}\\System.Runtime.WindowsRuntime.dll"
            "/reference:${MC_FRAMEWORK64_NATIVE}\\System.Runtime.InteropServices.WindowsRuntime.dll"
            "/reference:${MC_WINDOWS_WINMD_NATIVE}"
            "${MC_MEDIA_HELPER_SOURCE_NATIVE}"
        DEPENDS media-helper/WindowsMediaBridge.cs VERBATIM)
    add_custom_target(mc_windows_media_bridge ALL DEPENDS "${MC_MEDIA_HELPER}")
    add_dependencies(MinecraftOverlayManager mc_windows_media_bridge)
endif()

if(WIN32 AND NOT MC_OVERLAY_CONSOLE)
    set_target_properties(MinecraftOverlayManager PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Keep the user-facing entry point named main.qml while embedding the whole UI
# in the executable. Relative component lookup continues to work from /qml.
qt_add_resources(MinecraftOverlayManager "qml_resources"
    PREFIX "/"
    FILES
        qml/main.qml
        qml/Startup.qml
        qml/ProcessCard.qml
        qml/MaterialButton.qml
        qml/RippleEffect.qml
        qml/HypixelStatsCard.qml
        qml/PlayerStatusPage.qml
        qml/SkinAvatar.qml
        qml/WheelPage.qml
        qml/PageWheelHandler.qml
        qml/TextGuiPreview.qml
        qml/ScanIndicator.qml
        qml/ConfigPage.qml
        qml/MaterialTextField.qml
        qml/KeyCaptureButton.qml
)

target_compile_features(MinecraftOverlayManager PRIVATE cxx_std_20)
target_compile_definitions(MinecraftOverlayManager PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(MinecraftOverlayManager PRIVATE src)
if(MC_OVERLAY_UI_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    target_link_libraries(MinecraftOverlayManager PRIVATE Qt6::Test)
    target_compile_definitions(MinecraftOverlayManager PRIVATE MC_OVERLAY_UI_TESTS)
endif()

target_link_libraries(MinecraftOverlayManager PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Quick3D
    Qt6::Network
    Qt6::Concurrent
)

if(WIN32)
    # psapi supplies GetProcessMemoryInfo. user32 and dwmapi are used for
    # process-window association, WinEvent hooks, and transparent composition.
    target_link_libraries(MinecraftOverlayManager PRIVATE
        user32
        dwmapi
        psapi
        crypt32
    )
endif()

option(MC_OVERLAY_FETCH_AGENT_DEPENDENCIES
    "Fetch pinned MinHook and Dear ImGui sources for the native agent" ON)
if(NOT DEFINED MC_AGENT_FETCH_DEPENDENCIES)
    set(MC_AGENT_FETCH_DEPENDENCIES
        "${MC_OVERLAY_FETCH_AGENT_DEPENDENCIES}" CACHE BOOL
        "Fetch pinned MinHook and Dear ImGui sources while configuring")
endif()

add_subdirectory(agent)
add_subdirectory(attach-helper)
add_subdirectory(native-loader)
add_subdirectory(tests EXCLUDE_FROM_ALL)

# Keep the deployment layout deterministic. OverlayManager locates these files
# relative to the controller executable, so running directly from the build
# tree exercises the same paths as a packaged installation.
add_custom_target(mc_overlay_runtime_files ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:MinecraftOverlayManager>/agent"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:MinecraftOverlayManager>/tools"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:McOverlayAgent>"
            "$<TARGET_FILE_DIR:MinecraftOverlayManager>/agent/McOverlayAgent.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${MC_OVERLAY_ATTACH_HELPER_JAR}"
            "$<TARGET_FILE_DIR:MinecraftOverlayManager>/tools/McOverlayAttachHelper.jar"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:McOverlayNativeLoader>"
            "$<TARGET_FILE_DIR:MinecraftOverlayManager>/tools/McOverlayNativeLoader.exe"
    DEPENDS
        McOverlayAgent
        McOverlayNativeLoader
        mc_overlay_attach_helper
    VERBATIM)
add_dependencies(MinecraftOverlayManager mc_overlay_runtime_files)

install(TARGETS MinecraftOverlayManager
    RUNTIME DESTINATION .
)
install(TARGETS McOverlayAgent RUNTIME DESTINATION agent)
install(TARGETS McOverlayNativeLoader RUNTIME DESTINATION tools)
install(FILES "${MC_OVERLAY_ATTACH_HELPER_JAR}" DESTINATION tools)
if(WIN32)
    install(PROGRAMS "${MC_MEDIA_HELPER}" DESTINATION tools)
endif()
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/../NOTICE"
    "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/"
    DESTINATION third_party)
if(EXISTS "${MC_AGENT_MINHOOK_LICENSE_FILE}")
    install(FILES "${MC_AGENT_MINHOOK_LICENSE_FILE}"
        DESTINATION licenses RENAME MinHook-LICENSE.txt)
endif()
if(EXISTS "${MC_AGENT_IMGUI_LICENSE_FILE}")
    install(FILES "${MC_AGENT_IMGUI_LICENSE_FILE}"
        DESTINATION licenses RENAME Dear-ImGui-LICENSE.txt)
endif()
if(EXISTS "${CMAKE_ROOT}/Licenses/LGPLv3.txt")
    install(FILES "${CMAKE_ROOT}/Licenses/LGPLv3.txt"
        DESTINATION licenses RENAME Qt-LGPL-3.0.txt)
endif()
