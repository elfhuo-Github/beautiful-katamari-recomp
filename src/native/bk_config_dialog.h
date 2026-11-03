// beautiful_katamari - ImGui configuration dialog.
//
// A polished, intuitive GUI configuration utility built into the main app,
// toggled with F1. Organized into tabs: Native Renderer, Graphics, Audio,
// Input, and Advanced. Live cvar editing with save-to-config support.
//
// Built on the SDK's ImGuiDialog infrastructure.

#pragma once

#include <filesystem>
#include <string>

#include <rex/ui/imgui_dialog.h>

namespace bk {

// Configuration dialog for editing cvars at runtime.
//
// Tabs:
//   - Native Renderer: bk_native_render* cvars, frame pacing, debug viz
//   - Graphics: vsync, MSAA, render target path, readback, present mode
//   - Audio: XMA context patcher, capture toggle
//   - Input: MNK mode, keybind hints
//   - Advanced: all remaining cvars, searchable, restart-required flagging
//
// Changes take effect immediately for hot-reload cvars; restart-required
// cvars are flagged. "Save to config" writes to
// <executable dir>/beautiful_katamari.toml.
class ConfigDialog : public rex::ui::ImGuiDialog {
 public:
  explicit ConfigDialog(rex::ui::ImGuiDrawer* drawer);
  ~ConfigDialog() override;

  void SetVisible(bool visible) { visible_ = visible; }
  bool IsVisible() const { return visible_; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool visible_ = false;
  int active_tab_ = 0;  // 0=Native Renderer, 1=Graphics, 2=Audio, 3=Input, 4=Advanced
  std::filesystem::path config_path_;
  char search_buf_[128] = {};
  bool show_restart_required_ = true;

  // Tab renderers.
  void DrawNativeRendererTab();
  void DrawGraphicsTab();
  void DrawAudioTab();
  void DrawInputTab();
  void DrawAdvancedTab();

  // Draw a single cvar row with type-appropriate widget.
  void DrawCvar(const rex::cvar::FlagEntry& entry);

  // Save all cvars to the config TOML.
  void SaveConfig();
};

}  // namespace bk
