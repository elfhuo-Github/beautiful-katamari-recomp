// beautiful_katamari - Standalone configuration tool.
//
// A GUI configuration utility that runs before the game. Reads/writes
// beautiful_katamari.toml and can launch the game directly with the
// configured settings.
//
// Uses SDL3 + ImGui for the window/UI, and rex::cvar for reading/writing
// cvars into the TOML config file.

#pragma once

#include <string>
#include <vector>

#include <imgui.h>
#include <SDL3/SDL.h>

#include <rex/cvar.h>

namespace bk::config_tool {

// Edit a single cvar with a type-appropriate ImGui widget.
// Returns true if the value was changed.
bool EditCvar(const rex::cvar::FlagEntry& entry);

// Draw one category's cvars (grouped by category name).
void DrawCategory(const std::string& category,
                  const std::string& search,
                  bool show_restart_required);

// Main tool window. Runs its own SDL3 + ImGui loop.
int Run(int argc, char** argv);

}  // namespace bk::config_tool
