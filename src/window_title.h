// window_title.h - live status in the game window title.
#pragma once

#include <string_view>

#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace bk {

// Starts a background thread that updates the window title twice per second
// with "<static_prefix> | <fps> fps (<ms> ms) | CPU <n>% | rexglue <version>".
// Stops any previously started updater first.
void StartTitleUpdater(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                       std::string_view static_prefix);

// Stops the updater and flushes any queued title updates.  Must be called on
// the UI thread before the window is destroyed.
void StopTitleUpdater();

}  // namespace bk
