# beautiful_katamari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(beautiful_katamari LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use the rexglue-sdk source tree submodule as the SDK source when REXSDK_DIR
# is set to it. Otherwise, find_package() locates a prebuilt rexglue SDK install
# (the default for this repo).
#
# To build from source:
#   cmake -S . -B build -DREXSDK_DIR="${CMAKE_SOURCE_DIR}/third_party/rexglue-sdk"
#   (also requires: git submodule update --init --recursive in third_party/rexglue-sdk)
set(REXSDK_DIR "" CACHE PATH "Path to rexglue-sdk source tree (optional, uses prebuilt install if empty)")
include(generated/rexglue.cmake)

# Sources
set(BEAUTIFULKATAMARI_SOURCES
    src/audio_fix.cpp
    src/main.cpp
    src/video_fix.cpp
    src/window_title.cpp
    # Native renderer (inspired by skate3recomp, rewritten for BK)
    src/native/bk_native_render.cpp
    src/native/bk_native_scene.cpp
    src/native/bk_native_scene_gpu.cpp
    src/native/bk_native_guest_read.cpp
    # Configuration dialog (ImGui, toggled with F1)
    src/native/bk_config_dialog.cpp
)

if(WIN32)
    add_executable(beautiful_katamari WIN32 ${BEAUTIFULKATAMARI_SOURCES})
else()
    add_executable(beautiful_katamari ${BEAUTIFULKATAMARI_SOURCES})
endif()

# GPU_PLUGINS xenos stages librexgpu-xenos.so next to the executable and is
# what the runtime dlopen()s when the `gpu_plugin` cvar is set to "xenos".
# Skip codegen - generated sources already exist in generated/default/
set(GENERATED_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_register.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.0.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.1.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.4.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.5.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.6.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.7.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.8.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.9.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.10.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.11.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.12.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.13.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.14.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.15.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.17.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.18.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.19.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.20.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.21.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.22.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.23.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.24.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.25.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.26.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.27.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.28.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.29.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.30.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.31.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.33.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.34.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.35.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.36.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.37.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.38.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.39.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.40.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.41.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.42.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.43.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.44.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.45.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.46.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.47.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.48.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.49.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.50.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.51.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.52.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.53.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.54.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.55.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.56.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.57.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.58.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.59.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.60.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.61.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.62.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.63.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.64.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.65.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.66.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.67.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.68.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.69.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.70.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.71.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.72.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.73.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.74.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.75.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.76.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.77.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.78.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.79.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.80.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.81.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.82.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.83.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.84.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.85.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.86.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.87.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.88.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.89.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.90.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.91.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.92.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.93.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.94.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.95.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.96.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.97.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.98.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.99.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.100.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.101.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.102.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.103.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.104.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.105.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.106.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.107.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.108.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.109.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.110.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.111.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.112.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.113.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.114.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.115.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.116.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.117.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.118.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.119.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.120.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.121.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.122.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.123.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.124.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.125.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.126.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.127.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.128.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.129.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.130.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.131.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.132.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/beautiful_katamari_recomp.133.cpp
)
add_library(beautiful_katamari_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(beautiful_katamari_recomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default)
target_link_libraries(beautiful_katamari_recomp PRIVATE rex::runtime)
target_link_libraries(beautiful_katamari PRIVATE beautiful_katamari_recomp)


# src/window_title.cpp interposes rex::ui::Presenter::RefreshGuestOutput (the
# only per-guest-frame hook in the prebuilt runtime) to count frames for the
# live title bar.  Interposition only works if the executable exports the
# symbol in its dynamic symbol table, which is what --export-dynamic does; the
# real implementation is still reached via dlsym(RTLD_NEXT).
if(UNIX AND NOT APPLE)
    target_link_options(beautiful_katamari PRIVATE -Wl,--export-dynamic -Wl,-Bsymbolic-functions)
    # Wrap guest import thunks so our hooks can call the originals via __real_*.
    # Without this, dlsym(RTLD_NEXT) cannot see symbols defined in the same
    # executable, so the original guest functions never run (black screen).
    target_link_options(beautiful_katamari PRIVATE
        -Wl,--wrap=__imp__VdSwap
        -Wl,--wrap=__imp__VdInitializeRingBuffer
        -Wl,--wrap=__imp__VdEnableRingBufferRPtrWriteBack
        -Wl,--wrap=__imp__sub_824E04B0
        -Wl,--wrap=sub_824E04B0
        -Wl,--wrap=__imp__sub_824E5568
        -Wl,--wrap=sub_824E5568
        -Wl,--wrap=__imp__sub_824DF5F8
        -Wl,--wrap=sub_824DF5F8
    )
endif()

# Perf tuning for the recompiled guest code (host side).
#
# The SDK compiles generated code with -O3 -msse4.1 -mcmodel=large and no LTO.
# The large code model is not needed here: the executable's text is ~50 MB, so
# the small model's RIP-relative addressing is in range.  ThinLTO lets the
# per-function translation units inline the guest helper stubs (TLS getters,
# register save/restore helpers) that the profile shows hot.
if(UNIX AND NOT APPLE)
    target_compile_options(beautiful_katamari_recomp PRIVATE
        -march=native -mtune=native -mcmodel=small -flto=thin)
    target_link_options(beautiful_katamari PRIVATE -flto=thin)
endif()

# Native renderer include paths.
target_include_directories(beautiful_katamari PRIVATE
    src/native
    src/shaders
)

# When building the SDK from source, the SDK's install() step is what normally
# flattens its third-party headers into <prefix>/include (rex/ui/style.h pulls
# in imgui.h, o1heap.h, ...).  A source-tree build has no install prefix, so add
# those directories here to keep the host sources building unchanged.
if(REXSDK_DIR)
    target_include_directories(beautiful_katamari PRIVATE
        "${REXSDK_DIR}/thirdparty/imgui"
        "${REXSDK_DIR}/thirdparty/o1heap/o1heap"
        "${REXSDK_DIR}/thirdparty/disruptorplus/include"
        "${REXSDK_DIR}/thirdparty/renderdoc"
    )
endif()

# Standalone configuration tool (runs before the game, toggles cvars,
# saves to toml, and can launch the game).
add_executable(bk_config_tool
    src/config_tool/main.cpp
    src/config_tool/config_tool.cpp
)

target_include_directories(bk_config_tool PRIVATE
    src/config_tool
    ${CMAKE_CURRENT_SOURCE_DIR}
    -isystem /mnt/sdb/bkrecomp/sdk/linux-amd64/include
)

target_link_libraries(bk_config_tool PRIVATE rex::runtime)

if(UNIX AND NOT APPLE)
    target_link_libraries(bk_config_tool PRIVATE GL)
    target_link_options(bk_config_tool PRIVATE -Wl,--export-dynamic)
endif()

# Copy config.toml to build directory so SDK reads it at startup
if(UNIX AND NOT APPLE)
    add_custom_command(TARGET beautiful_katamari POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config.toml
            $<TARGET_FILE_DIR:beautiful_katamari>
        VERBATIM
    )
endif()

# The SDK sets an $ORIGIN runpath but only stages librexruntime.so next to the
# host on Windows/macOS. On Linux it must be copied beside the executable so
# the $ORIGIN runpath resolves it.
if(UNIX AND NOT APPLE)
    add_custom_command(TARGET beautiful_katamari POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rex::runtime>
            $<TARGET_FILE_DIR:beautiful_katamari>
        VERBATIM
    )
endif()

# Link Vulkan for the native renderer.
if(UNIX AND NOT APPLE)
    find_package(Vulkan REQUIRED)
    target_link_libraries(beautiful_katamari PRIVATE Vulkan::Vulkan)
endif()
