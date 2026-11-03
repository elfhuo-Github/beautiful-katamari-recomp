// beautiful_katamari - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cctype>
#include <cstdio>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/ui/virtual_key.h>

#include "native/bk_config_dialog.h"
#include "native/bk_native_render.h"
#include "window_title.h"

class BeautifulKatamariApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BeautifulKatamariApp>(new BeautifulKatamariApp(ctx, "beautiful_katamari",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:

  // Default to the Xenos GPU plugin when the user has not selected one. The
  // plugin is staged next to the executable by the CMake GPU_PLUGINS argument
  // and dlopen()ed here; this keeps a plain `beautiful_katamari
  // --game_data_root <dir>` run working without a sidecar config file.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    // Install the native renderer hook layer (bk_native_render). Guest
    // function hooks are registered here; capture is gated behind the
    // bk_native_render / bk_native_render_scene cvars.
    Install();
  }

  // Do NOT create the achievement toast dialog.
  //
  // `ReXApp::LaunchModule` constructs it unconditionally and keeps it alive for
  // the whole session. Every `ImGuiDialog` registers itself with the
  // `ImGuiDrawer` in its constructor, and `ImGuiDrawer::Draw` unconditionally
  // calls `Presenter::RequestUIPaintFromUIThread()` while any dialog exists.
  // That forms a repaint feedback loop:
  //
  //   paint -> Draw -> RequestUIPaint -> paint -> ...
  //
  // On Win32 the presenter throttles that loop to the monitor refresh rate
  // (`Presenter::WaitForUITickFromUIThread` waits on DXGI vblank). On Linux that
  // function is compiled out, so the loop spins as fast as the GPU can present.
  // With the default `VK_PRESENT_MODE_IMMEDIATE` swapchain (no vsync wait) that
  // was measured at ~25,000 presents per second by MangoHud, saturating the GPU
  // with 1280x720 blits and making the whole desktop sluggish.
  //
  // With no dialog registered, the presenter instead paints once per guest
  // frame from the guest GPU thread, so the host present rate equals the guest
  // frame rate (<= 60 Hz) and MangoHud reports a real number.
  //
  // Achievements are not available for this title in the recompiled build
  // anyway, so nothing user-visible is lost.
  //
  // Note for later: the F3 debug / F4 settings overlays are created on demand
  // and destroyed when closed, so they still work, but leaving one open
  // re-enables the unthrottled loop on Linux.
  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    return nullptr;
  }

  // Dump the graphics/present settings that actually took effect.  Cvar
  // defaults live in the prebuilt runtime libraries, and several of them have
  // a large effect on performance (native_2x_msaa, the render target path,
  // readback, present mode), so record them once at startup instead of
  // guessing.  All values are the effective post-config-file values.
  void OnPostSetup() override {
    static constexpr const char* kNames[] = {
        "vsync",
        "resolution_scale",
        "draw_resolution_scale_x",
        "draw_resolution_scale_y",
        "native_2x_msaa",
        "readback_resolve",
        "readback_memexport",
        "readback_memexport_fast",
        "occlusion_query_enable",
        "query_occlusion_fake_sample_count",
        "render_target_path_vulkan",
        "direct_host_resolve",
        "gamma_render_target_as_unorm16",
        "snorm16_render_target_full_range",
        "vulkan_dynamic_rendering",
        "vulkan_async_skip_incomplete_frames",
        "vulkan_submit_on_primary_buffer_end",
        "vulkan_pipeline_creation_threads",
        "vulkan_sparse_shared_memory",
        "vulkan_allow_present_mode_immediate",
        "vulkan_allow_present_mode_mailbox",
        "vulkan_allow_present_mode_fifo_relaxed",
        "present_effect",
        "present_dither",
        "present_letterbox",
        "present_render_pass_clear",
        "present_allow_overscan_cutoff",
        "present_safe_area_x",
        "present_safe_area_y",
        "video_mode_width",
        "video_mode_height",
        "video_mode_refresh_rate",
        "high_resolution",
        "host_present_from_non_ui_thread",
        "async_shader_compilation",
        "clear_memory_page_state",
    };
    std::fprintf(stderr, "[bk] effective graphics settings:\n");
    for (const char* name : kNames) {
      std::string value = rex::cvar::Query<std::string>(name);
      std::fprintf(stderr, "[bk]   %-40s = %s%s\n", name, value.c_str(),
                   value.empty() ? "(unset)" : "");
    }

    // Per-frame status in the title bar: the renderer, the guest output
    // resolution (the frontbuffer the title itself requests - the window size
    // has no effect on it), and a live frame rate from the guest presentation
    // hook.  Started here because the window exists by OnPostSetup.
    std::string render_target_path = rex::cvar::Query<std::string>("render_target_path_vulkan");
    for (char& c : render_target_path) {
      c = char(std::toupper(static_cast<unsigned char>(c)));
    }
    std::string static_prefix = "Beautiful Katamari | Vulkan";
    if (!render_target_path.empty()) {
      static_prefix += " " + render_target_path;
    }
    static_prefix += " " + rex::cvar::Query<std::string>("video_mode_width") + "x" +
                     rex::cvar::Query<std::string>("video_mode_height");
    bk::StartTitleUpdater(window(), app_context(), static_prefix);
  }

  void OnShutdown() override { bk::StopTitleUpdater(); }

  // Create the configuration dialog and add it to the ImGui drawer.
  // The dialog starts hidden; press F1 to toggle it.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    config_dialog_ = std::make_unique<bk::ConfigDialog>(drawer);
    drawer->AddDialog(config_dialog_.get());
  }

  // Toggle the configuration dialog with F1.
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() == rex::ui::VirtualKey::kF1) {
      config_dialog_visible_ = !config_dialog_visible_;
      if (config_dialog_) {
        config_dialog_->SetVisible(config_dialog_visible_);
      }
      // Don't let the event propagate further (F1 is handled by us).
      return;
    }
  }

  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::unique_ptr<bk::ConfigDialog> config_dialog_;
  bool config_dialog_visible_ = false;
};
