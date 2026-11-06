cmake_minimum_required(VERSION 3.21)

if(APPLE)
    # ScreenCaptureKit needs macOS 12.3. Universal binary: Resolume 7.11+ runs natively on Apple Silicon.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.3" CACHE STRING "")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

project(ResolumeSlideCapture VERSION 1.0 LANGUAGES C CXX)

# Windows: static C++ runtime (matches FFGL's glew32s.lib and needs no VC++ redistributable).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- FFGL SDK
# Pinned to a known commit of github.com/resolume/ffgl (master, supports Resolume 7.4+).
# Offline? Clone it yourself and pass -DFFGL_SDK_DIR=path/to/ffgl
set(FFGL_SDK_DIR "" CACHE PATH "Path to a checkout of github.com/resolume/ffgl")
if(NOT FFGL_SDK_DIR)
    include(FetchContent)
    FetchContent_Declare(ffgl
        GIT_REPOSITORY https://github.com/resolume/ffgl.git
        GIT_TAG        5b6e07840277df9ec35a228b9eb067636316350f
        SOURCE_SUBDIR  do-not-add-subdirectory)
    FetchContent_MakeAvailable(ffgl)
    set(FFGL_SDK_DIR ${ffgl_SOURCE_DIR})
endif()

# Interface target: every plugin compiles FFGLSDK.cpp itself so that plugMain
# is guaranteed to end up (exported) in each binary.
add_library(ffglsdk INTERFACE)
target_include_directories(ffglsdk INTERFACE ${FFGL_SDK_DIR}/source/lib)
set(FFGL_SDK_SOURCE ${FFGL_SDK_DIR}/source/lib/FFGLSDK.cpp)

if(WIN32)
    target_include_directories(ffglsdk INTERFACE ${FFGL_SDK_DIR}/deps/glew-2.1.0/include)
    target_compile_definitions(ffglsdk INTERFACE GLEW_STATIC _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(ffglsdk INTERFACE ${FFGL_SDK_DIR}/deps/glew-2.1.0/lib/Release/x64/glew32s.lib opengl32)
elseif(APPLE)
    target_compile_definitions(ffglsdk INTERFACE TARGET_OS_MAC=1 GL_SILENCE_DEPRECATION)
    target_link_libraries(ffglsdk INTERFACE "-framework OpenGL")
else()
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(ffglsdk INTERFACE GLEW::GLEW OpenGL::GL)
endif()

# ---------------------------------------------------------------- shared code
set(COMMON_SOURCES
    src/common/PlatformCommon.cpp
    src/common/DocumentPrep.cpp
    src/common/FrameRenderer.cpp
)

if(WIN32)
    set(PLATFORM_SOURCES
        src/win/PlatformWin.cpp
        src/win/CaptureWin.cpp
        src/win/PdfWin.cpp
        src/win/PowerPointWin.cpp)
    set(PLATFORM_LIBS d3d11 dxgi dwmapi windowsapp ole32 oleaut32 shell32 user32)
elseif(APPLE)
    set(PLATFORM_SOURCES
        src/posix/PlatformPosix.cpp
        src/mac/PlatformMac.mm
        src/mac/CaptureMac.mm
        src/mac/PdfMac.mm
        src/mac/PowerPointMac.mm)
    set(PLATFORM_LIBS
        "-framework Foundation" "-framework AppKit" "-framework CoreGraphics"
        "-framework CoreMedia" "-framework CoreVideo" "-framework ScreenCaptureKit")
else()
    set(PLATFORM_SOURCES src/posix/PlatformPosix.cpp src/linux/StubsLinux.cpp)
    set(PLATFORM_LIBS pthread)
endif()

add_library(slidecapture_core STATIC ${COMMON_SOURCES} ${PLATFORM_SOURCES})
target_link_libraries(slidecapture_core PUBLIC ffglsdk ${PLATFORM_LIBS})
if(APPLE)
    target_compile_options(slidecapture_core PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif()
if(MSVC)
    target_compile_options(slidecapture_core PUBLIC /EHsc /permissive- /utf-8 /bigobj /Zc:__cplusplus)
    target_compile_definitions(slidecapture_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------- plugins
function(add_ffgl_plugin target output_name bundle_id)
    add_library(${target} MODULE ${ARGN} ${FFGL_SDK_SOURCE})
    target_link_libraries(${target} PRIVATE slidecapture_core)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}" PREFIX "")
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION bundle
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME "${output_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}"
            MACOSX_BUNDLE_EXECUTABLE_NAME "${output_name}")
    endif()
    install(TARGETS ${target} LIBRARY DESTINATION . BUNDLE DESTINATION .)
endfunction()

add_ffgl_plugin(sc_screen_capture "SCScreenCapture" "com.slidecapture.screencapture"
    src/plugins/ScreenCapturePlugin.cpp)
add_ffgl_plugin(sc_presentation "SCPresentation" "com.slidecapture.presentation"
    src/plugins/PresentationPlugin.cpp)

# ---------------------------------------------------------------- tests (Linux only)
option(SC_BUILD_TESTS "Build the headless FFGL smoke test host (Linux)" OFF)
if(SC_BUILD_TESTS AND UNIX AND NOT APPLE)
    add_executable(ffgl_host_smoke tests/ffgl_host_smoke.cpp)
    target_include_directories(ffgl_host_smoke PRIVATE ${FFGL_SDK_DIR}/source/lib)
    target_link_libraries(ffgl_host_smoke PRIVATE EGL GLEW::GLEW OpenGL::GL ${CMAKE_DL_LIBS} pthread)
endif()
