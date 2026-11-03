// beautiful_katamari - ImGui configuration dialog (implementation).

#include "bk_config_dialog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk {

ConfigDialog::ConfigDialog(rex::ui::ImGuiDrawer* drawer)
    : rex::ui::ImGuiDialog(drawer) {
  config_path_ = std::filesystem::current_path() / "beautiful_katamari.toml";
}

ConfigDialog::~ConfigDialog() = default;

namespace {

// Returns true if the value was changed.
bool EditCvar(const rex::cvar::FlagEntry& entry) {
  std::string current = rex::cvar::GetFlagByName(entry.name);
  std::string new_value = current;
  bool edited = false;

  switch (entry.type) {
    case rex::cvar::FlagType::Boolean: {
      bool b = (current == "true" || current == "1");
      if (ImGui::Checkbox("##edit", &b)) {
        new_value = b ? "true" : "false";
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::Int32: {
      int32_t v = 0;
      try {
        v = std::stoi(current);
      } catch (...) {
        v = 0;
      }
      if (ImGui::InputInt("##edit", &v)) {
        new_value = std::to_string(v);
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::Int64: {
      int64_t v = 0;
      try {
        v = std::stoll(current);
      } catch (...) {
        v = 0;
      }
      if (ImGui::InputScalar("##edit", ImGuiDataType_S64, &v)) {
        new_value = std::to_string(v);
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::Uint32: {
      uint32_t v = 0;
      try {
        v = std::stoul(current);
      } catch (...) {
        v = 0;
      }
      if (ImGui::InputScalar("##edit", ImGuiDataType_U32, &v)) {
        new_value = std::to_string(v);
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::Uint64: {
      uint64_t v = 0;
      try {
        v = std::stoull(current);
      } catch (...) {
        v = 0;
      }
      if (ImGui::InputScalar("##edit", ImGuiDataType_U64, &v)) {
        new_value = std::to_string(v);
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::Double: {
      double v = 0.0;
      try {
        v = std::stod(current);
      } catch (...) {
        v = 0.0;
      }
      if (ImGui::InputDouble("##edit", &v, 0.0, 0.0, "%g")) {
        new_value = std::to_string(v);
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::String: {
      static thread_local char buf[256];
      std::strncpy(buf, current.c_str(), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      if (ImGui::InputText("##edit", buf, sizeof(buf))) {
        new_value = buf;
        edited = true;
      }
      break;
    }
    case rex::cvar::FlagType::Command:
      ImGui::TextDisabled("(command)");
      break;
  }

  if (edited && entry.type != rex::cvar::FlagType::Command) {
    if (rex::cvar::SetFlagByName(entry.name, new_value)) {
      REXLOG_INFO("config: set {} = {}", entry.name, new_value);
    } else {
      REXLOG_WARN("config: failed to set {} = {}", entry.name, new_value);
    }
  }
  return edited;
}

bool CvarMatchesSearch(const rex::cvar::FlagEntry& entry,
                       const std::string& search) {
  if (search.empty()) return true;
  std::string name_lower = entry.name;
  std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  std::string desc_lower = entry.description;
  std::transform(desc_lower.begin(), desc_lower.end(), desc_lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return name_lower.find(search) != std::string::npos ||
         desc_lower.find(search) != std::string::npos;
}

}  // namespace

void ConfigDialog::OnDraw(ImGuiIO& io) {
  (void)io;

  if (!visible_) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Beautiful Katamari - Configuration", &visible_,
                    ImGuiWindowFlags_MenuBar)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save to config", "Ctrl+S")) {
        SaveConfig();
      }
      if (ImGui::MenuItem("Reset all to defaults")) {
        rex::cvar::ResetAllToDefaults();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Close", "F1")) {
        visible_ = false;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About")) {
        ImGui::OpenPopup("about_popup");
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  // About popup.
  if (ImGui::BeginPopupModal("about_popup", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Beautiful Katamari Recompilation");
    ImGui::Text("Native renderer configuration utility");
    ImGui::Separator();
    ImGui::Text("Built with rexglue-sdk v0.10.0");
    ImGui::Text("Inspired by skate3recomp");
    ImGui::Separator();
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::Spacing();

  // Tab bar.
  const char* tab_names[] = {
      "Native Renderer", "Graphics", "Audio", "Input", "Advanced",
  };
  for (int i = 0; i < 5; ++i) {
    if (i > 0) ImGui::SameLine();
    bool selected = (active_tab_ == i);
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
    }
    if (ImGui::Button(tab_names[i], ImVec2(150, 0))) {
      active_tab_ = i;
    }
    if (selected) {
      ImGui::PopStyleColor();
    }
  }

  ImGui::Separator();

  // Main content.
  switch (active_tab_) {
    case 0:
      DrawNativeRendererTab();
      break;
    case 1:
      DrawGraphicsTab();
      break;
    case 2:
      DrawAudioTab();
      break;
    case 3:
      DrawInputTab();
      break;
    case 4:
      DrawAdvancedTab();
      break;
  }

  ImGui::End();
}

void ConfigDialog::DrawNativeRendererTab() {
  ImGui::Text("Native renderer (inspired by skate3recomp, rewritten for BK)");
  ImGui::Separator();

  // Define the native renderer cvars we want to feature here.
  struct NRCvar {
    const char* name;
    const char* label;
    const char* description;
  };

  constexpr NRCvar kNativeCvars[] = {
      {"bk_native_render", "Enable native renderer",
       "Install guest render hooks and enable the native renderer layer."},
      {"bk_native_render_scene", "Render scene natively",
       "Replace the emulated xenos GPU output with native Vulkan rendering."},
      {"bk_native_render_scene_debug", "Debug visualization",
       "0=normal, 1=clear only, 2=solid per item, 3=limit 32 items, "
       "4=disable depth test."},
      {"bk_guest_fps_cap", "Guest FPS cap",
       "Cap the guest render loop to this frame rate (0 = uncapped). "
       "Capping a few fps below display refresh smooths VRR judder."},
  };

  for (const auto& cv : kNativeCvars) {
    // Find the entry in the registry.
    const rex::cvar::FlagEntry* entry = rex::cvar::GetFlagInfo(cv.name);
    if (!entry) {
      ImGui::TextDisabled("%s (not registered)", cv.name);
      continue;
    }

    bool restart_required =
        (entry->lifecycle == rex::cvar::Lifecycle::kRequiresRestart);
    if (restart_required) {
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[restart] ");
      ImGui::SameLine();
    }

    ImGui::PushID(cv.name);
    ImGui::Text("%s", cv.label);
    ImGui::SameLine();
    ImGui::TextDisabled(" - %s", cv.description);

    EditCvar(*entry);

    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::Separator();
  ImGui::TextDisabled(
      "The native renderer hooks guest render functions (__imp__VdSwap, "
      "sub_824E04B0, ring buffer init) and renders the scene with Vulkan "
      "instead of the emulated xenos GPU path.");
}

void ConfigDialog::DrawGraphicsTab() {
  ImGui::Text("Graphics / presentation settings");
  ImGui::Separator();

  constexpr const char* kGraphicsCvars[] = {
      "vsync",
      "resolution_scale",
      "draw_resolution_scale_x",
      "draw_resolution_scale_y",
      "native_2x_msaa",
      "render_target_path_vulkan",
      "readback_resolve",
      "readback_memexport",
      "readback_memexport_fast",
      "occlusion_query_enable",
      "query_occlusion_fake_sample_count",
      "direct_host_resolve",
      "present_effect",
      "present_letterbox",
      "present_dither",
      "present_safe_area_x",
      "present_safe_area_y",
      "window_width",
      "window_height",
      "fullscreen",
      nullptr,
  };

  for (int i = 0; kGraphicsCvars[i] != nullptr; ++i) {
    const rex::cvar::FlagEntry* entry =
        rex::cvar::GetFlagInfo(kGraphicsCvars[i]);
    if (!entry) continue;

    bool restart_required =
        (entry->lifecycle == rex::cvar::Lifecycle::kRequiresRestart);
    if (restart_required) {
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[restart] ");
      ImGui::SameLine();
    }

    ImGui::PushID(kGraphicsCvars[i]);
    ImGui::Text("%s", kGraphicsCvars[i]);
    ImGui::SameLine();
    ImGui::TextDisabled(" - %s", entry->description.c_str());

    EditCvar(*entry);

    ImGui::PopID();
    ImGui::Spacing();
  }
}

void ConfigDialog::DrawAudioTab() {
  ImGui::Text("Audio settings");
  ImGui::Separator();

  constexpr const char* kAudioCvars[] = {
      "mnk_mode",
      nullptr,
  };

  for (int i = 0; kAudioCvars[i] != nullptr; ++i) {
    const rex::cvar::FlagEntry* entry =
        rex::cvar::GetFlagInfo(kAudioCvars[i]);
    if (!entry) continue;

    ImGui::PushID(kAudioCvars[i]);
    ImGui::Text("%s", kAudioCvars[i]);
    ImGui::SameLine();
    ImGui::TextDisabled(" - %s", entry->description.c_str());

    EditCvar(*entry);

    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::Separator();
  ImGui::TextDisabled(
      "Audio fixes (XMA loop_count patcher, render frame capture) are always "
      "active and not cvar-controlled.");
}

void ConfigDialog::DrawInputTab() {
  ImGui::Text("Input settings");
  ImGui::Separator();

  constexpr const char* kInputCvars[] = {
      "mnk_mode",
      nullptr,
  };

  for (int i = 0; kInputCvars[i] != nullptr; ++i) {
    const rex::cvar::FlagEntry* entry =
        rex::cvar::GetFlagInfo(kInputCvars[i]);
    if (!entry) continue;

    ImGui::PushID(kInputCvars[i]);
    ImGui::Text("%s", kInputCvars[i]);
    ImGui::SameLine();
    ImGui::TextDisabled(" - %s", entry->description.c_str());

    EditCvar(*entry);

    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::Separator();
  ImGui::TextDisabled(
      "Keyboard/mouse -> Xbox 360 controller emulation (MNK):\n"
      "  A = Space / Semicolon\n"
      "  B = Backspace / Quote\n"
      "  D-pad = Arrow keys (Shift+arrows in some builds)\n"
      "  Start = Enter\n"
      "  Back = Tab\n"
      "  Triggers = Q/I and E/O\n"
      "  Sticks = WASD-style keys, mouse for right stick");
}

void ConfigDialog::DrawAdvancedTab() {
  ImGui::Text("Advanced - all cvars");
  ImGui::Separator();

  // Search bar.
  ImGui::PushItemWidth(200);
  ImGui::InputTextWithHint("##adv_search", "Search cvars...", search_buf_,
                           sizeof(search_buf_));
  ImGui::PopItemWidth();
  ImGui::SameLine();
  ImGui::Checkbox("Show restart-required", &show_restart_required_);
  ImGui::SameLine();
  ImGui::TextDisabled("| F1 to toggle dialog");

  ImGui::Separator();

  std::string search = search_buf_;
  std::transform(search.begin(), search.end(), search.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // Group by category.
  std::set<std::string> categories;
  for (const auto& entry : rex::cvar::GetRegistry()) {
    categories.insert(entry.category);
  }

  for (const auto& category : categories) {
    // Collect matching cvars in this category.
    std::vector<const rex::cvar::FlagEntry*> matches;
    for (const auto& entry : rex::cvar::GetRegistry()) {
      if (entry.category != category) continue;
      if (!CvarMatchesSearch(entry, search)) continue;
      if (!show_restart_required_ &&
          entry.lifecycle == rex::cvar::Lifecycle::kRequiresRestart) {
        continue;
      }
      matches.push_back(&entry);
    }
    if (matches.empty()) continue;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
    if (ImGui::TreeNodeEx(category.c_str(), flags)) {
      for (const auto* entry : matches) {
        bool restart_required =
            (entry->lifecycle == rex::cvar::Lifecycle::kRequiresRestart);
        if (restart_required) {
          ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[restart] ");
          ImGui::SameLine();
        }

        ImGui::PushID(entry->name.c_str());
        ImGui::Text("%s", entry->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(" - %s", entry->description.c_str());

        EditCvar(*entry);

        ImGui::PopID();
      }
      ImGui::TreePop();
    }
  }
}

void ConfigDialog::SaveConfig() {
  std::string toml = rex::cvar::SerializeToTOML();
  std::error_code ec;
  std::filesystem::create_directories(config_path_.parent_path(), ec);
  std::ofstream out(config_path_);
  if (out) {
    out << toml;
    out.close();
    REXLOG_INFO("config: saved to {}", config_path_.string());
  } else {
    REXLOG_ERROR("config: failed to write {}", config_path_.string());
  }
}

}  // namespace bk
