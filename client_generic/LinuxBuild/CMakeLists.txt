cmake_minimum_required(VERSION 3.20)
project(infinidream LANGUAGES C CXX)

# Suppress deprecated FindBoost warning on CMake 3.30+
cmake_policy(SET CMP0167 NEW)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(INFINIDREAM_ENABLE_RIFE "Enable Linux RIFE frame generation via ncnn Vulkan" OFF)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
add_compile_definitions(
    LINUX_GNU
    BOOST_FILESYSTEM_VERSION=3
    BOOST_NETWORK_ENABLE_HTTPS
    OPENSSL_NO_ERR
    "SHAREDIR=\"./\""
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# ---------------------------------------------------------------------------
# Find required packages
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Boost — filesystem + thread (boost_system is header-only since 1.69)
find_package(Boost REQUIRED COMPONENTS filesystem thread log log_setup json)

# FFmpeg — try pkg-config first, then manual paths
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
    )
endif()

# Wayland — optional, enables native Wayland surface (VK_KHR_wayland_surface).
# Falls back to X11/XWayland if not found. XScreensaver mode always uses X11.
if(PKG_CONFIG_FOUND)
    pkg_check_modules(WAYLAND_CLIENT wayland-client)
    pkg_check_modules(WAYLAND_CURSOR wayland-cursor)
    pkg_check_modules(WAYLAND_PROTOCOLS wayland-protocols)
    pkg_check_modules(XKB xkbcommon)
    pkg_check_modules(LIBDECOR libdecor-0)
    find_program(WAYLAND_SCANNER wayland-scanner HINTS /usr/bin /usr/local/bin)
    if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_FOUND AND WAYLAND_SCANNER)
        message(STATUS "Wayland found — native Wayland surface enabled")
        pkg_get_variable(WL_PROTOCOLS_DIR wayland-protocols pkgdatadir)
        set(XDG_SHELL_XML "${WL_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
        add_custom_command(
            OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
                    ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} private-code
                    ${XDG_SHELL_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
            COMMAND ${WAYLAND_SCANNER} client-header
                    ${XDG_SHELL_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h
            DEPENDS ${XDG_SHELL_XML}
            COMMENT "Generating xdg-shell protocol bindings"
        )
        set(XDG_DECORATION_XML "${WL_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
        add_custom_command(
            OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c
                    ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h
            COMMAND ${WAYLAND_SCANNER} private-code
                    ${XDG_DECORATION_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c
            COMMAND ${WAYLAND_SCANNER} client-header
                    ${XDG_DECORATION_XML} ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-client-protocol.h
            DEPENDS ${XDG_DECORATION_XML}
            COMMENT "Generating xdg-decoration protocol bindings"
        )
        add_compile_definitions(HAVE_WAYLAND)
        if(LIBDECOR_FOUND)
            message(STATUS "libdecor found — Wayland client-side decoration fallback enabled")
            add_compile_definitions(HAVE_LIBDECOR)
        else()
            message(STATUS "libdecor not found — Wayland fallback decorations disabled")
        endif()
    else()
        message(STATUS "Wayland not found — X11/XWayland only")
    endif()
endif()

# libpng
find_package(PNG)
if(NOT PNG_FOUND)
    pkg_check_modules(PNG libpng)
endif()

# libcurl
find_package(CURL REQUIRED)

# ---------------------------------------------------------------------------
# Shader compilation (requires glslc from the Vulkan SDK or shaderc)
# ---------------------------------------------------------------------------
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin /usr/bin /usr/local/bin)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../DisplayOutput/Vulkan/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

if(GLSLC)
    message(STATUS "Found glslc: ${GLSLC}")
    add_custom_command(
        OUTPUT  ${SHADER_OUT_DIR}/quad.vert.spv
        COMMAND ${GLSLC} -o ${SHADER_OUT_DIR}/quad.vert.spv
                         ${SHADER_SRC_DIR}/quad.vert
        DEPENDS ${SHADER_SRC_DIR}/quad.vert
        COMMENT "Compiling quad.vert → SPIR-V"
    )
    add_custom_command(
        OUTPUT  ${SHADER_OUT_DIR}/quad.frag.spv
        COMMAND ${GLSLC} -o ${SHADER_OUT_DIR}/quad.frag.spv
                         ${SHADER_SRC_DIR}/quad.frag
        DEPENDS ${SHADER_SRC_DIR}/quad.frag
        COMMENT "Compiling quad.frag → SPIR-V"
    )
    add_custom_target(compile_shaders ALL DEPENDS
        ${SHADER_OUT_DIR}/quad.vert.spv
        ${SHADER_OUT_DIR}/quad.frag.spv
    )
else()
    message(WARNING "glslc not found. Shaders will not be compiled automatically.")
    message(WARNING "Install the Vulkan SDK or run: apt install glslc")
    add_custom_target(compile_shaders)
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------

# Path shortcuts
set(BASE    ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CLIENT  ${BASE}/Client)
set(COMMON  ${BASE}/Common)
set(MATH    ${BASE}/Common/Math)
set(DISPLAY ${BASE}/DisplayOutput)
set(VULKAN  ${BASE}/DisplayOutput/Vulkan)
set(RENDERER ${BASE}/DisplayOutput/Renderer)
set(CDEC    ${BASE}/ContentDecoder)
set(CDOWN   ${BASE}/ContentDownloader)
set(NET     ${BASE}/Networking)
set(STORE   ${BASE}/TupleStorage)
set(FGEN    ${BASE}/FrameGeneration)
set(LINBLD  ${CMAKE_CURRENT_SOURCE_DIR})
set(LINUX_DEPS_CACHE_DIR ${LINBLD}/deps-cache)
set(LINUX_MODEL_CACHE_DIR ${LINBLD}/model-cache)
set(RIFE_RUNTIME_MODEL_NAME "rife-v4.6")
set(RIFE_RUNTIME_MODELS_DIR "${CMAKE_CURRENT_BINARY_DIR}/models")
set(RIFE_RUNTIME_MODEL_DIR "${RIFE_RUNTIME_MODELS_DIR}/${RIFE_RUNTIME_MODEL_NAME}")

set(RIFE_EXTRA_INCLUDE_DIRS)
set(RIFE_EXTRA_LIBS)
set(RIFE_EXTRA_DEPENDENCIES)

if(INFINIDREAM_ENABLE_RIFE)
    file(MAKE_DIRECTORY "${LINUX_DEPS_CACHE_DIR}")
    file(MAKE_DIRECTORY "${LINUX_MODEL_CACHE_DIR}")

    set(NCNN_TAG "20240102")
    option(NCNN_INSTALL_SDK "" OFF)
    option(NCNN_PIXEL_ROTATE "" OFF)
    option(NCNN_PIXEL_AFFINE "" OFF)
    option(NCNN_PIXEL_DRAWING "" OFF)
    option(NCNN_VULKAN "" ON)
    option(NCNN_VULKAN_ONLINE_SPIRV "" ON)
    option(NCNN_BUILD_BENCHMARK "" OFF)
    option(NCNN_BUILD_TESTS "" OFF)
    option(NCNN_BUILD_TOOLS "" OFF)
    option(NCNN_BUILD_EXAMPLES "" OFF)
    option(NCNN_DISABLE_RTTI "" OFF)
    option(NCNN_DISABLE_EXCEPTION "" OFF)
    option(NCNN_INT8 "" OFF)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum policy version for fetched deps" FORCE)
    FetchContent_Declare(
        ncnn_src
        URL https://github.com/Tencent/ncnn/releases/download/${NCNN_TAG}/ncnn-${NCNN_TAG}-full-source.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${LINUX_DEPS_CACHE_DIR}/ncnn-full-source-${NCNN_TAG}"
    )
    FetchContent_MakeAvailable(ncnn_src)

    set(NCNN_GLSLANG_SPVBUILDER_H
        "${ncnn_src_SOURCE_DIR}/glslang/SPIRV/SpvBuilder.h")
    if(EXISTS "${NCNN_GLSLANG_SPVBUILDER_H}")
        file(READ "${NCNN_GLSLANG_SPVBUILDER_H}" NCNN_SPVBUILDER_CONTENTS)
        if(NOT NCNN_SPVBUILDER_CONTENTS MATCHES "#include <cstdint>")
            string(REPLACE "#include <stack>"
                           "#include <stack>\n#include <cstdint>"
                           NCNN_SPVBUILDER_CONTENTS
                           "${NCNN_SPVBUILDER_CONTENTS}")
            file(WRITE "${NCNN_GLSLANG_SPVBUILDER_H}" "${NCNN_SPVBUILDER_CONTENTS}")
        endif()
    endif()

    set(RIFE_UPSTREAM_TAG "20221029")
    FetchContent_Declare(
        rife_upstream
        URL https://github.com/nihui/rife-ncnn-vulkan/archive/refs/tags/${RIFE_UPSTREAM_TAG}.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${LINUX_DEPS_CACHE_DIR}/rife-ncnn-vulkan-${RIFE_UPSTREAM_TAG}"
    )
    FetchContent_MakeAvailable(rife_upstream)

    set(RIFE_SRC_DIR "${rife_upstream_SOURCE_DIR}/src")
    set(RIFE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/rife-upstream")
    file(MAKE_DIRECTORY "${RIFE_BINARY_DIR}")

    macro(infinidream_rife_add_shader shader_src)
        get_filename_component(shader_name_we "${shader_src}" NAME_WE)
        set(shader_comp_header "${RIFE_BINARY_DIR}/${shader_name_we}.comp.hex.h")
        add_custom_command(
            OUTPUT "${shader_comp_header}"
            COMMAND ${CMAKE_COMMAND}
                -DSHADER_SRC=${RIFE_SRC_DIR}/${shader_src}
                -DSHADER_COMP_HEADER=${shader_comp_header}
                -P ${RIFE_SRC_DIR}/generate_shader_comp_header.cmake
            DEPENDS ${RIFE_SRC_DIR}/${shader_src}
                    ${RIFE_SRC_DIR}/generate_shader_comp_header.cmake
            COMMENT "Generating ${shader_name_we}.comp.hex.h"
            VERBATIM
        )
        set_source_files_properties("${shader_comp_header}" PROPERTIES GENERATED TRUE)
        list(APPEND RIFE_SHADER_HEADERS "${shader_comp_header}")
    endmacro()

    infinidream_rife_add_shader(rife_preproc.comp)
    infinidream_rife_add_shader(rife_postproc.comp)
    infinidream_rife_add_shader(rife_preproc_tta.comp)
    infinidream_rife_add_shader(rife_postproc_tta.comp)
    infinidream_rife_add_shader(rife_flow_tta_avg.comp)
    infinidream_rife_add_shader(rife_v2_flow_tta_avg.comp)
    infinidream_rife_add_shader(rife_v4_flow_tta_avg.comp)
    infinidream_rife_add_shader(rife_flow_tta_temporal_avg.comp)
    infinidream_rife_add_shader(rife_v2_flow_tta_temporal_avg.comp)
    infinidream_rife_add_shader(rife_v4_flow_tta_temporal_avg.comp)
    infinidream_rife_add_shader(rife_out_tta_temporal_avg.comp)
    infinidream_rife_add_shader(rife_v4_timestep.comp)
    infinidream_rife_add_shader(rife_v4_timestep_tta.comp)
    infinidream_rife_add_shader(warp.comp)
    infinidream_rife_add_shader(warp_pack4.comp)
    infinidream_rife_add_shader(warp_pack8.comp)

    add_library(infinidream_rife_backend STATIC
        ${RIFE_SRC_DIR}/rife.cpp
        ${RIFE_SRC_DIR}/warp.cpp
        ${RIFE_SHADER_HEADERS}
    )
    target_include_directories(infinidream_rife_backend PUBLIC
        ${RIFE_SRC_DIR}
        ${RIFE_BINARY_DIR}
    )
    target_link_libraries(infinidream_rife_backend PUBLIC
        ncnn
        Vulkan::Vulkan
        Threads::Threads
    )

    set(RIFE_EXTRA_INCLUDE_DIRS
        ${RIFE_SRC_DIR}
        ${RIFE_BINARY_DIR}
    )
    set(RIFE_EXTRA_LIBS infinidream_rife_backend)
    set(RIFE_EXTRA_DEPENDENCIES infinidream_rife_backend)
    set(RIFE_UPSTREAM_MODEL_DIR "${rife_upstream_SOURCE_DIR}/models/${RIFE_RUNTIME_MODEL_NAME}")
endif()

set(SOURCES
    # Entry point
    ${CLIENT}/main.cpp

    # Core client
    ${CLIENT}/Player.cpp
    ${CLIENT}/Hud.cpp
    ${CLIENT}/StringFormat.cpp

    # Common
    ${COMMON}/AlignedBuffer.cpp
    ${COMMON}/Common.cpp
    ${COMMON}/Exception.cpp
    ${COMMON}/Log.cpp
    ${COMMON}/pool.cpp
    ${MATH}/Rect.cpp

    # Display output — base
    ${DISPLAY}/DisplayOutput.cpp
    ${DISPLAY}/Image.cpp
    ${DISPLAY}/LoadPNG.cpp
    ${DISPLAY}/LoadDDS.cpp

    # Renderer base classes
    ${RENDERER}/Renderer.cpp
    ${RENDERER}/Font.cpp
    ${RENDERER}/Shader.cpp
    ${RENDERER}/Text.cpp
    ${RENDERER}/Texture.cpp
    ${RENDERER}/TextureFlat.cpp

    # Vulkan backend
    ${VULKAN}/DisplayVulkan.cpp
    ${VULKAN}/RendererVulkan.cpp
    ${VULKAN}/TextureFlatVulkan.cpp
    ${VULKAN}/ShaderVulkan.cpp
    ${VULKAN}/FontVulkan.cpp

    # ImGui — compiled directly from the submodule source
    ${BASE}/imgui/imgui.cpp
    ${BASE}/imgui/imgui_draw.cpp
    ${BASE}/imgui/imgui_tables.cpp
    ${BASE}/imgui/imgui_widgets.cpp
    ${BASE}/imgui/backends/imgui_impl_vulkan.cpp

    # Content decoder (ffmpeg)
    ${CDEC}/Clip.cpp
    ${CDEC}/ContentDecoder.cpp

    # Frame generation
    ${FGEN}/BlendFrameInterpolator.cpp
    ${FGEN}/FrameGenerationScheduler.cpp
    ${FGEN}/RifeInterpolatorNcnn.cpp

    # Content downloader
    ${CDOWN}/CacheManager.cpp
    ${CDOWN}/ContentDownloader.cpp
    ${CDOWN}/DreamDownloader.cpp
    ${CDOWN}/MessageQueue.cpp
    ${CDOWN}/PathManager.cpp
    ${CDOWN}/PlaylistManager.cpp
    ${CDOWN}/ServerConfig.cpp

    # Networking
    ${NET}/Download.cpp
    ${NET}/EDreamClient.cpp
    ${NET}/Networking.cpp
    ${NET}/Upload.cpp

    # Storage
    ${STORE}/JSONStorage.cpp
    ${STORE}/storage.cpp

    # Linux platform utilities
    ${LINBLD}/PlatformUtils_Linux.cpp
)

# Append generated Wayland protocol bindings when Wayland is available
if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_FOUND AND WAYLAND_SCANNER)
    list(APPEND SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/xdg-decoration-unstable-v1-protocol.c
    )
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(infinidream ${SOURCES})
add_dependencies(infinidream compile_shaders)
if(RIFE_EXTRA_DEPENDENCIES)
    add_dependencies(infinidream ${RIFE_EXTRA_DEPENDENCIES})
endif()

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(infinidream PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}           # LinuxBuild (for PlatformUtils_Internal.h)
    ${CLIENT}
    ${COMMON}
    ${MATH}
    ${DISPLAY}
    ${DISPLAY}/Renderer
    ${DISPLAY}/Vulkan
    ${CDEC}
    ${CDOWN}
    ${NET}
    ${STORE}
    ${BASE}                               # client_generic root
    ${BASE}/MacBuild                         # OSD.hpp, etc.
    ${BASE}/libpng/include
    ${BASE}/socket.io-client-cpp/include
    ${BASE}/websocketpp
    ${BASE}/boost
    ${VULKAN_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}           # generated xdg-shell-client-protocol.h
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_CURSOR_INCLUDE_DIRS}
    ${LIBDECOR_INCLUDE_DIRS}
    ${XKB_INCLUDE_DIRS}
    ${BASE}/imgui
    ${BASE}/imgui/backends
    ${RIFE_EXTRA_INCLUDE_DIRS}
)

target_compile_definitions(infinidream PRIVATE
    $<$<BOOL:${INFINIDREAM_ENABLE_RIFE}>:INFINIDREAM_ENABLE_RIFE=1>
)

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
target_link_libraries(infinidream PRIVATE
    Vulkan::Vulkan
    ${X11_LIBRARIES}
    ${X11_Xrender_LIB}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::filesystem
    Boost::thread
    Boost::log
    Boost::log_setup
    Boost::json
    ${CURL_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    swscale
    ${PNG_LIBRARIES}
    dl
    ${BASE}/socket.io-client-cpp/lib/linux/libsioclient_tls.a
    $<$<BOOL:${WAYLAND_CLIENT_FOUND}>:${WAYLAND_CLIENT_LIBRARIES}>
    $<$<BOOL:${WAYLAND_CURSOR_FOUND}>:${WAYLAND_CURSOR_LIBRARIES}>
    $<$<BOOL:${LIBDECOR_FOUND}>:${LIBDECOR_LIBRARIES}>
    $<$<BOOL:${XKB_FOUND}>:${XKB_LIBRARIES}>
    ${RIFE_EXTRA_LIBS}
)

# ---------------------------------------------------------------------------
# RPATH so the binary finds its .so files without installing
# ---------------------------------------------------------------------------
set_target_properties(infinidream PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

# ---------------------------------------------------------------------------
# Copy shaders next to the binary after build
# ---------------------------------------------------------------------------
if(GLSLC)
    add_custom_command(TARGET infinidream POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_OUT_DIR}
            $<TARGET_FILE_DIR:infinidream>/shaders
        COMMENT "Copying compiled shaders to binary directory"
    )
endif()

# Copy runtime assets next to the binary
add_custom_command(TARGET infinidream POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${BASE}/Runtime/logo.png
        $<TARGET_FILE_DIR:infinidream>/logo.png
    COMMENT "Copying runtime assets to binary directory"
)

if(INFINIDREAM_ENABLE_RIFE)
    if(EXISTS "${RIFE_UPSTREAM_MODEL_DIR}")
        file(COPY "${RIFE_UPSTREAM_MODEL_DIR}" DESTINATION "${LINUX_MODEL_CACHE_DIR}")
        add_custom_command(TARGET infinidream POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${RIFE_RUNTIME_MODELS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LINUX_MODEL_CACHE_DIR}/${RIFE_RUNTIME_MODEL_NAME}"
                "${RIFE_RUNTIME_MODEL_DIR}"
            COMMENT "Copying bundled RIFE model to binary directory"
        )
    else()
        message(FATAL_ERROR "Expected RIFE model directory missing: ${RIFE_UPSTREAM_MODEL_DIR}")
    endif()
endif()
