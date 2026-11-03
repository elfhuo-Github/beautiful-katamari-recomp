// beautiful_katamari - Standalone configuration tool implementation.

#include "config_tool.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>
#include <SDL3/SDL.h>

#if defined(__linux__) && !defined(__APPLE__)
#include <GL/gl.h>
#else
#include <OpenGL/gl3.h>
#endif

#include <rex/cvar.h>

namespace bk::config_tool {
namespace {

//------------------------------------------------------------------------------
// Paths
//------------------------------------------------------------------------------

std::filesystem::path g_config_path;
std::filesystem::path g_game_executable;

void InitPaths() {
  const char* base_path = SDL_GetBasePath();
  if (base_path) {
    g_config_path = base_path;
    g_game_executable = base_path;
    SDL_free((void*)base_path);
  }
  g_config_path /= "beautiful_katamari.toml";
  g_game_executable /= "beautiful_katamari";
}

//------------------------------------------------------------------------------
// Minimal ImGui SDL3 + OpenGL3 backends (inlined)
//------------------------------------------------------------------------------

SDL_Window* g_sdl_window = nullptr;
SDL_GLContext g_gl_context = nullptr;

bool ImGui_ImplSDL3_InitForOpenGL(SDL_Window* window) {
  g_sdl_window = window;
  return true;
}

bool ImGui_ImplSDL3_ProcessEvent(const SDL_Event* event) {
  ImGuiIO& io = ImGui::GetIO();
  if (event->type == SDL_EVENT_KEY_DOWN) {
    SDL_Keycode key = event->key.key;
    if (key >= SDLK_SPACE && key <= SDLK_Z) {
      io.AddInputCharacter(static_cast<unsigned int>(key));
    }
    io.AddKeyEvent(static_cast<ImGuiKey>(key), true);
  } else if (event->type == SDL_EVENT_KEY_UP) {
    io.AddKeyEvent(static_cast<ImGuiKey>(event->key.key), false);
  } else if (event->type == SDL_EVENT_TEXT_INPUT) {
    io.AddInputCharactersUTF8(event->edit.text);
  } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
    io.AddMousePosEvent(static_cast<float>(event->motion.x),
                        static_cast<float>(event->motion.y));
  } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
    io.AddMouseWheelEvent(static_cast<float>(event->wheel.x),
                          static_cast<float>(event->wheel.y));
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    io.AddMouseButtonEvent(event->button.button - 1, true);
  } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
    io.AddMouseButtonEvent(event->button.button - 1, false);
  }
  return true;
}

void ImGui_ImplSDL3_NewFrame() {
  ImGuiIO& io = ImGui::GetIO();
  int w, h;
  SDL_GetWindowSize(g_sdl_window, &w, &h);
  io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
}

// OpenGL function pointers (loaded via SDL_GL_GetProcAddress for core profile)
typedef void (APIENTRY *PFNGLUSEPROGRAM)(GLuint);
typedef void (APIENTRY *PFNGLBINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY *PFNGLBINDTEXTURE)(GLenum, GLuint);
typedef void (APIENTRY *PFNGLSCISSOR)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *PFNGLDRAWELEMENTS)(GLenum, GLsizei, GLenum, const void*);
typedef void (APIENTRY *PFNGLENABLE)(GLenum);
typedef void (APIENTRY *PFNGLDISABLE)(GLenum);
typedef void (APIENTRY *PFNGLBLENDEQUATION)(GLenum);
typedef void (APIENTRY *PFNGLBLENDFUNC)(GLenum, GLenum);
typedef void (APIENTRY *PFNGLPOLYGONMODE)(GLenum, GLenum);
typedef void (APIENTRY *PFNGLGETINTEGERV)(GLenum, GLint*);
typedef void (APIENTRY *PFNGLGETBOOLEANV)(GLenum, GLboolean*);
typedef void (APIENTRY *PFNGLVMATRIXMODE)(GLenum);
typedef void (APIENTRY *PFNGLLOADMATRIXF)(const GLfloat*);
typedef void (APIENTRY *PFNGLLOADIDENTITY)(void);
typedef void (APIENTRY *PFNGLVIEWPORT)(GLint, GLint, GLsizei, GLsizei);
typedef void (APIENTRY *PFNGLCLEARCOLOR)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFNGLCLEAR)(GLbitfield);

static PFNGLUSEPROGRAM p_glUseProgram = nullptr;
static PFNGLBINDBUFFER p_glBindBuffer = nullptr;
static PFNGLBINDTEXTURE p_glBindTexture = nullptr;
static PFNGLSCISSOR p_glScissor = nullptr;
static PFNGLDRAWELEMENTS p_glDrawElements = nullptr;
static PFNGLENABLE p_glEnable = nullptr;
static PFNGLDISABLE p_glDisable = nullptr;
static PFNGLBLENDEQUATION p_glBlendEquation = nullptr;
static PFNGLBLENDFUNC p_glBlendFunc = nullptr;
static PFNGLPOLYGONMODE p_glPolygonMode = nullptr;
static PFNGLGETINTEGERV p_glGetIntegerv = nullptr;
static PFNGLGETBOOLEANV p_glGetBooleanv = nullptr;
static PFNGLVMATRIXMODE p_glMatrixMode = nullptr;
static PFNGLLOADMATRIXF p_glLoadMatrixf = nullptr;
static PFNGLLOADIDENTITY p_glLoadIdentity = nullptr;
static PFNGLVIEWPORT p_glViewport = nullptr;
static PFNGLCLEARCOLOR p_glClearColor = nullptr;
static PFNGLCLEAR p_glClear = nullptr;

static void LoadGLFunctions() {
  p_glUseProgram = (PFNGLUSEPROGRAM)SDL_GL_GetProcAddress("glUseProgram");
  p_glBindBuffer = (PFNGLBINDBUFFER)SDL_GL_GetProcAddress("glBindBuffer");
  p_glBindTexture = (PFNGLBINDTEXTURE)SDL_GL_GetProcAddress("glBindTexture");
  p_glScissor = (PFNGLSCISSOR)SDL_GL_GetProcAddress("glScissor");
  p_glDrawElements = (PFNGLDRAWELEMENTS)SDL_GL_GetProcAddress("glDrawElements");
  p_glEnable = (PFNGLENABLE)SDL_GL_GetProcAddress("glEnable");
  p_glDisable = (PFNGLDISABLE)SDL_GL_GetProcAddress("glDisable");
  p_glBlendEquation = (PFNGLBLENDEQUATION)SDL_GL_GetProcAddress("glBlendEquation");
  p_glBlendFunc = (PFNGLBLENDFUNC)SDL_GL_GetProcAddress("glBlendFunc");
  p_glPolygonMode = (PFNGLPOLYGONMODE)SDL_GL_GetProcAddress("glPolygonMode");
  p_glGetIntegerv = (PFNGLGETINTEGERV)SDL_GL_GetProcAddress("glGetIntegerv");
  p_glGetBooleanv = (PFNGLGETBOOLEANV)SDL_GL_GetProcAddress("glGetBooleanv");
  p_glMatrixMode = (PFNGLVMATRIXMODE)SDL_GL_GetProcAddress("glMatrixMode");
  p_glLoadMatrixf = (PFNGLLOADMATRIXF)SDL_GL_GetProcAddress("glLoadMatrixf");
  p_glLoadIdentity = (PFNGLLOADIDENTITY)SDL_GL_GetProcAddress("glLoadIdentity");
  p_glViewport = (PFNGLVIEWPORT)SDL_GL_GetProcAddress("glViewport");
  p_glClearColor = (PFNGLCLEARCOLOR)SDL_GL_GetProcAddress("glClearColor");
  p_glClear = (PFNGLCLEAR)SDL_GL_GetProcAddress("glClear");
}

bool ImGui_ImplOpenGL3_Init() {
  LoadGLFunctions();
  return true;
}

void ImGui_ImplOpenGL3_NewFrame() {
  ImGuiIO& io = ImGui::GetIO();
  int w, h;
  SDL_GetWindowSizeInPixels(g_sdl_window, &w, &h);
  io.DisplayFramebufferScale = ImVec2(
      w > 0 ? static_cast<float>(w) / io.DisplaySize.x : 1.0f,
      h > 0 ? static_cast<float>(h) / io.DisplaySize.y : 1.0f);
}

void ImGui_ImplOpenGL3_SetupRenderState(const ImDrawData* draw_data) {
  const ImGuiIO& io = ImGui::GetIO();
  p_glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
  const float ortho_projection[4][4] = {
      {2.0f / io.DisplaySize.x, 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / -io.DisplaySize.y, 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
  };
  p_glMatrixMode(GL_PROJECTION);
  p_glLoadMatrixf(&ortho_projection[0][0]);
  p_glMatrixMode(GL_MODELVIEW);
  p_glLoadIdentity();
  p_glMatrixMode(GL_TEXTURE);
  p_glLoadIdentity();
}

void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data) {
  GLint last_program = 0;
  GLint last_texture = 0;
  GLint last_array_buffer = 0;
  GLint last_element_array_buffer = 0;
  GLint last_viewport[4] = {0};
  GLint last_scissor_box[4] = {0};
  GLboolean last_blend = GL_FALSE;
  GLboolean last_scissor_test = GL_FALSE;
  p_glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
  p_glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
  p_glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
  p_glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &last_element_array_buffer);
  p_glGetIntegerv(GL_VIEWPORT, last_viewport);
  p_glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
  p_glGetBooleanv(GL_BLEND, &last_blend);
  p_glGetBooleanv(GL_SCISSOR_TEST, &last_scissor_test);

  p_glEnable(GL_BLEND);
  p_glBlendEquation(GL_FUNC_ADD);
  p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  p_glDisable(GL_CULL_FACE);
  p_glDisable(GL_DEPTH_TEST);
  p_glDisable(GL_STENCIL_TEST);
  p_glEnable(GL_SCISSOR_TEST);
  p_glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  ImGui_ImplOpenGL3_SetupRenderState(draw_data);

  for (int n = 0; n < draw_data->CmdListsCount; n++) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    const ImDrawIdx* idx_buffer_offset = 0;

    for (const ImDrawCmd& pcmd : cmd_list->CmdBuffer) {
      if (pcmd.UserCallback != nullptr) {
        pcmd.UserCallback(cmd_list, &pcmd);
      } else {
        ImGuiIO& io = ImGui::GetIO();
        GLuint tex_id = static_cast<GLuint>(pcmd.GetTexID());
        p_glBindTexture(GL_TEXTURE_2D, tex_id);
        p_glScissor((int)(pcmd.ClipRect.x),
                    (int)(io.DisplaySize.y - pcmd.ClipRect.w),
                    (int)(pcmd.ClipRect.z - pcmd.ClipRect.x),
                    (int)(pcmd.ClipRect.w - pcmd.ClipRect.y));
        p_glDrawElements(GL_TRIANGLES, pcmd.ElemCount, GL_UNSIGNED_SHORT,
                         idx_buffer_offset);
      }
      idx_buffer_offset += pcmd.ElemCount;
    }
  }

  p_glUseProgram(last_program);
  p_glBindTexture(GL_TEXTURE_2D, last_texture);
  p_glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
  p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, last_element_array_buffer);
  p_glViewport(last_viewport[0], last_viewport[1], last_viewport[2],
               last_viewport[3]);
  p_glScissor(last_scissor_box[0], last_scissor_box[1], last_scissor_box[2],
              last_scissor_box[3]);
  p_glDisable(GL_SCISSOR_TEST);
  if (last_blend)
    p_glEnable(GL_BLEND);
  else
    p_glDisable(GL_BLEND);
  if (last_scissor_test)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);
}

void ImGui_ImplOpenGL3_Shutdown() {
  if (g_gl_context) {
    SDL_GL_DestroyContext(g_gl_context);
    g_gl_context = nullptr;
  }
}

void ImGui_ImplSDL3_Shutdown() {
  // Nothing to shutdown for the minimal SDL3 backend.
}

}  // namespace (anonymous)

//------------------------------------------------------------------------------
// Cvar widget (in bk::config_tool, not anonymous namespace)
//------------------------------------------------------------------------------

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
    if (!rex::cvar::SetFlagByName(entry.name, new_value)) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                         "Failed to set %s", entry.name.c_str());
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

//------------------------------------------------------------------------------
// Featured cvar lists
//------------------------------------------------------------------------------

struct FeaturedCvar {
  const char* name;
  const char* label;
  const char* description;
};

constexpr FeaturedCvar kNativeRendererCvars[] = {
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

constexpr FeaturedCvar kGraphicsCvars[] = {
    {"vsync", "VSync", "Enable vertical sync"},
    {"resolution_scale", "Resolution scale", "Render resolution multiplier"},
    {"draw_resolution_scale_x", "Draw resolution X",
     "Horizontal draw resolution scale"},
    {"draw_resolution_scale_y", "Draw resolution Y",
     "Vertical draw resolution scale"},
    {"native_2x_msaa", "Native 2x MSAA", "Enable 2x MSAA"},
    {"render_target_path_vulkan", "Render target path",
     "fbo or fsi (fragment shader interlock)"},
    {"readback_resolve", "Readback resolve",
     "none, full, fast, or some"},
    {"occlusion_query_enable", "Occlusion queries",
     "Enable host occlusion queries"},
    {"present_effect", "Present effect",
     "bilinear, cas, fsr2, or fsr3"},
    {"window_width", "Window width", "Window width in pixels"},
    {"window_height", "Window height", "Window height in pixels"},
    {"fullscreen", "Fullscreen", "Fullscreen mode"},
};

constexpr FeaturedCvar kAudioCvars[] = {
    {"mnk_mode", "MnK mode",
     "Keyboard/mouse controller emulation (0=gamepad, 1=keyboard+mouse)"},
};

constexpr FeaturedCvar kInputCvars[] = {
    {"mnk_mode", "MnK mode",
     "Keyboard/mouse controller emulation (0=gamepad, 1=keyboard+mouse)"},
};

//------------------------------------------------------------------------------
// Save config
//------------------------------------------------------------------------------

void SaveConfig() {
  std::string toml = rex::cvar::SerializeToTOML();
  std::error_code ec;
  std::filesystem::create_directories(g_config_path.parent_path(), ec);
  std::ofstream out(g_config_path);
  if (out) {
    out << toml;
    out.close();
  }
}

//------------------------------------------------------------------------------
// Draw a category of cvars
//------------------------------------------------------------------------------

void DrawCategory(const std::string& category,
                  const std::string& search,
                  bool show_restart_required) {
  std::vector<const rex::cvar::FlagEntry*> matches;
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.category != category) continue;
    if (!CvarMatchesSearch(entry, search)) continue;
    if (!show_restart_required &&
        entry.lifecycle == rex::cvar::Lifecycle::kRequiresRestart) {
      continue;
    }
    matches.push_back(&entry);
  }
  if (matches.empty()) return;

  if (ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
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

//------------------------------------------------------------------------------
// Main tool
//------------------------------------------------------------------------------

int Run(int argc, char** argv) {
  (void)argc;
  (void)argv;

  InitPaths();

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  SDL_Window* window = SDL_CreateWindow(
      "Beautiful Katamari - Configuration", 1280, 800,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  if (!window) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  g_gl_context = SDL_GL_CreateContext(window);
  if (!g_gl_context) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n",
                 SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_GL_MakeCurrent(window, g_gl_context);
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOpenGL(window);
  ImGui_ImplOpenGL3_Init();

  bool running = true;
  bool show_window = true;
  int active_tab = 0;
  const char* tab_names[] = {"Native Renderer", "Graphics", "Audio",
                             "Input",     "Advanced"};
  char search_buf[128] = {};
  bool show_restart_required = true;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (show_window) {
      ImGui::SetNextWindowSize(ImVec2(1100, 750), ImGuiCond_FirstUseEver);
      if (!ImGui::Begin("Beautiful Katamari - Configuration", &show_window,
                        ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
      } else {
        if (ImGui::BeginMenuBar()) {
          if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
              SaveConfig();
            }
            if (ImGui::MenuItem("Save & Launch Game")) {
              SaveConfig();
              std::string cmd = g_game_executable.string();
              cmd += " --game_data_root=\"\"";
              std::system(cmd.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
              running = false;
            }
            ImGui::EndMenu();
          }
          if (ImGui::BeginMenu("View")) {
            ImGui::Checkbox("Show restart-required cvars",
                            &show_restart_required);
            ImGui::EndMenu();
          }
          ImGui::EndMenuBar();
        }

        ImGui::Spacing();

        for (int i = 0; i < 5; ++i) {
          if (i > 0) ImGui::SameLine();
          bool selected = (active_tab == i);
          if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
          }
          if (ImGui::Button(tab_names[i], ImVec2(150, 0))) {
            active_tab = i;
          }
          if (selected) {
            ImGui::PopStyleColor();
          }
        }

        ImGui::Separator();

        if (active_tab == 4) {
          ImGui::PushItemWidth(200);
          ImGui::InputTextWithHint("##adv_search", "Search cvars...", search_buf,
                                   sizeof(search_buf));
          ImGui::PopItemWidth();
          ImGui::SameLine();
          ImGui::Checkbox("Show restart-required", &show_restart_required);
          ImGui::SameLine();
          ImGui::TextDisabled("Config: %s", g_config_path.string().c_str());
          ImGui::Separator();
        }

        std::string search = search_buf;
        std::transform(search.begin(), search.end(), search.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        switch (active_tab) {
          case 0: {
            ImGui::Text("Native renderer (inspired by skate3recomp)");
            ImGui::Separator();
            for (const auto& cv : kNativeRendererCvars) {
              const rex::cvar::FlagEntry* entry =
                  rex::cvar::GetFlagInfo(cv.name);
              if (!entry) {
                ImGui::TextDisabled("%s (not registered)", cv.name);
                continue;
              }
              bool restart_required =
                  (entry->lifecycle ==
                   rex::cvar::Lifecycle::kRequiresRestart);
              if (restart_required) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                   "[restart] ");
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
                "The native renderer hooks guest render functions "
                "(__imp__VdSwap, sub_824E04B0, ring buffer init) and renders "
                "the scene with Vulkan instead of the emulated xenos GPU path.");
            break;
          }
          case 1: {
            ImGui::Text("Graphics / presentation settings");
            ImGui::Separator();
            for (const auto& cv : kGraphicsCvars) {
              const rex::cvar::FlagEntry* entry =
                  rex::cvar::GetFlagInfo(cv.name);
              if (!entry) {
                ImGui::TextDisabled("%s (not registered)", cv.name);
                continue;
              }
              bool restart_required =
                  (entry->lifecycle ==
                   rex::cvar::Lifecycle::kRequiresRestart);
              if (restart_required) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                   "[restart] ");
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
            break;
          }
          case 2: {
            ImGui::Text("Audio settings");
            ImGui::Separator();
            for (const auto& cv : kAudioCvars) {
              const rex::cvar::FlagEntry* entry =
                  rex::cvar::GetFlagInfo(cv.name);
              if (!entry) continue;
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
                "Audio fixes (XMA loop_count patcher, render frame capture) "
                "are always active.");
            break;
          }
          case 3: {
            ImGui::Text("Input settings");
            ImGui::Separator();
            for (const auto& cv : kInputCvars) {
              const rex::cvar::FlagEntry* entry =
                  rex::cvar::GetFlagInfo(cv.name);
              if (!entry) continue;
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
                "Keyboard/mouse -> Xbox 360 controller emulation (MNK):\n"
                "  A = Space / Semicolon\n"
                "  B = Backspace / Quote\n"
                "  D-pad = Arrow keys\n"
                "  Start = Enter\n"
                "  Back = Tab\n"
                "  Triggers = Q/I and E/O\n"
                "  Sticks = WASD-style keys, mouse for right stick\n"
                "\nFor a DualShock 4 (054c:09cc), set mnk_mode=0 and provide a "
                "gamecontrollerdb.txt next to the executable.");
            break;
          }
          case 4: {
            DrawCategory("Beautiful Katamari", search,
                         show_restart_required);
            DrawCategory("Skate 3", search, show_restart_required);
            DrawCategory("Graphics", search, show_restart_required);
            DrawCategory("Audio", search, show_restart_required);
            break;
          }
        }

        ImGui::End();
      }

      p_glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
      p_glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
      p_glClear(GL_COLOR_BUFFER_BIT);
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      SDL_GL_SwapWindow(window);

      if (!show_window) {
        running = false;
      }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(g_gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
  }
}

}  // namespace bk::config_tool
