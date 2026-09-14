cmake_minimum_required(VERSION 3.24)
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(McOverlayNavigationTrajectoryTests EXCLUDE_FROM_ALL NavigationTrajectoryTests.cpp)
target_compile_features(McOverlayNavigationTrajectoryTests PRIVATE cxx_std_20)
add_executable(McOverlaySkinAndWheelTests EXCLUDE_FROM_ALL
    SkinAndWheelTests.cpp ../src/SkinCuboidGeometry.cpp ../src/SkinCuboidGeometry.h)
target_compile_features(McOverlaySkinAndWheelTests PRIVATE cxx_std_20)
target_compile_definitions(McOverlaySkinAndWheelTests PRIVATE MC_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(McOverlaySkinAndWheelTests PRIVATE
    Qt6::Gui Qt6::Quick Qt6::Quick3D Qt6::QuickControls2 Qt6::Test)

add_executable(McOverlayAimControlTests EXCLUDE_FROM_ALL AimControlTests.cpp)
target_compile_features(McOverlayAimControlTests PRIVATE cxx_std_20)
add_executable(McOverlayKnockbackTests EXCLUDE_FROM_ALL KnockbackEvidenceTests.cpp)
target_compile_features(McOverlayKnockbackTests PRIVATE cxx_std_20)

add_executable(McOverlayRegressionPolicyTests EXCLUDE_FROM_ALL RegressionPolicyTests.cpp)
target_compile_features(McOverlayRegressionPolicyTests PRIVATE cxx_std_20)
target_link_libraries(McOverlayRegressionPolicyTests PRIVATE Qt6::Core)

if(NOT WIN32)
    return()
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "McOverlayOpenGlJvmSmoke requires a Windows x64 build.")
endif()

add_executable(McOverlayLogicalPipelineHookTests EXCLUDE_FROM_ALL
    LogicalPipelineHookTests.cpp)
target_compile_features(McOverlayLogicalPipelineHookTests PRIVATE cxx_std_20)
target_compile_definitions(McOverlayLogicalPipelineHookTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(McOverlayLogicalPipelineHookTests PRIVATE
    "${MC_AGENT_JNI_INCLUDE_DIR}" "${MC_AGENT_JNI_PLATFORM_INCLUDE_DIR}")

add_executable(McOverlayControllerResponsivenessTests EXCLUDE_FROM_ALL
    ControllerResponsivenessTests.cpp ../src/OverlayManager.cpp ../src/OverlayManager.h
    ../src/ProcessScanner.cpp ../src/ProcessScanner.h)
target_compile_features(McOverlayControllerResponsivenessTests PRIVATE cxx_std_20)
target_compile_definitions(McOverlayControllerResponsivenessTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(McOverlayControllerResponsivenessTests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent user32 dwmapi psapi crypt32)

# Keep this target opt-in: normal application builds and packages should not
# acquire an extra test executable. Build it explicitly with
# `cmake --build <build-dir> --target McOverlayOpenGlJvmSmoke`.
add_executable(McOverlayOpenGlJvmSmoke EXCLUDE_FROM_ALL
    OpenGlJvmSmokeHarness.cpp)
set_target_properties(McOverlayOpenGlJvmSmoke PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF)
target_compile_features(McOverlayOpenGlJvmSmoke PRIVATE cxx_std_20)
target_compile_definitions(McOverlayOpenGlJvmSmoke PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(McOverlayOpenGlJvmSmoke PRIVATE
    "${MC_AGENT_JNI_INCLUDE_DIR}"
    "${MC_AGENT_JNI_PLATFORM_INCLUDE_DIR}")
target_link_libraries(McOverlayOpenGlJvmSmoke PRIVATE
    gdi32
    opengl32
    user32)
add_dependencies(McOverlayOpenGlJvmSmoke McOverlayAgent)

add_executable(McOverlayHotkeyCaptureTests EXCLUDE_FROM_ALL
    HotkeyCaptureServiceTests.cpp
    ../src/HotkeyCaptureService.cpp
    ../src/HotkeyCaptureService.h)
target_compile_features(McOverlayHotkeyCaptureTests PRIVATE cxx_std_20)
target_include_directories(McOverlayHotkeyCaptureTests PRIVATE ../src)
target_link_libraries(McOverlayHotkeyCaptureTests PRIVATE Qt6::Core Qt6::Gui)

# Isolated WGL rendering regression: no JVM, injection, game or network access.
add_executable(McOverlayRendererTests EXCLUDE_FROM_ALL
    OverlayRendererTests.cpp
    ../agent/overlay_renderer.cpp ../agent/wndproc_hook.cpp
    ../agent/gaussian_blur.cpp ../agent/tsf_candidates.cpp
    ../agent/src/AgentLog.cpp
    $<TARGET_OBJECTS:mc_agent_assets>)
target_compile_features(McOverlayRendererTests PRIVATE cxx_std_20)
target_compile_definitions(McOverlayRendererTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_include_directories(McOverlayRendererTests PRIVATE
    ../agent "${MC_AGENT_JNI_INCLUDE_DIR}" "${MC_AGENT_JNI_PLATFORM_INCLUDE_DIR}")
target_link_libraries(McOverlayRendererTests PRIVATE mc_agent::imgui Qt6::Gui
    opengl32 gdi32 user32 dwmapi imm32 ole32 oleaut32 uuid advapi32)

if(MSVC)
    target_compile_options(McOverlayOpenGlJvmSmoke PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(McOverlayOpenGlJvmSmoke PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(MINGW)
        target_link_options(McOverlayOpenGlJvmSmoke PRIVATE
            -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()
