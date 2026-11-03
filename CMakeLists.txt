# beautiful_katamari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(beautiful_katamari LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/native/bk_native_palette.cpp
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
rexglue_setup_target(beautiful_katamari GPU_PLUGINS xenos)

# src/window_title.cpp interposes rex::ui::Presenter::RefreshGuestOutput (the
# only per-guest-frame hook in the prebuilt runtime) to count frames for the
# live title bar.  Interposition only works if the executable exports the
# symbol in its dynamic symbol table, which is what --export-dynamic does; the
# real implementation is still reached via dlsym(RTLD_NEXT).
if(UNIX AND NOT APPLE)
    target_link_options(beautiful_katamari PRIVATE -Wl,--export-dynamic)
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

