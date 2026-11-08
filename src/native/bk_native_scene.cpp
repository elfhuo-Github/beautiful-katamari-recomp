// beautiful_katamari - native renderer scene builder (game-thread half).

#include "bk_native_scene.h"
#include "bk_native_render.h"
#include "bk_native_guest_read.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native {
namespace {

std::mutex g_build_mutex;
FrameScene g_scene;
uint64_t g_generation = 0;
uint64_t g_draw_sequence = 0;

// Mesh context structure offsets in guest memory.
// These are Beautiful Katamari-specific and must match the guest's actual
// MeshContext layout. Tunable via cvars for reverse-engineering.
//
// Offset values from CTX dump of ctx=0x40006D00:
//   +0x10: vertex buffer byte count (4992)
//   +0x14: unknown (1673704)
//   +0x18: vertex stride (8)
//   +0x24: 524288 (index count?)
//   +0x30: vertex buffer address
//   +0x38: index buffer address
//   +0x3C: index count (reads 1 - likely wrong)
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_vb_addr, 0x30, "Beautiful Katamari",
    "Mesh context offset for vertex buffer address.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_vb_bytes, 0x10, "Beautiful Katamari",
    "Mesh context offset for vertex buffer byte count.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_ib_addr, 0x38, "Beautiful Katamari",
    "Mesh context offset for index buffer address.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_mesh_ctx_ib_count, 0x24, "Beautiful Katamari",
    "Mesh context offset for index count.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Camera view/projection matrix addresses in guest memory.
// The game stores view_proj as 16 floats (column-major, 64 bytes).
// Tunable via cvars for reverse-engineering. Set to 0 to auto-scan.
REXCVAR_DEFINE_UINT32(bk_native_camera_view_proj_addr, 0, "Beautiful Katamari",
    "Guest address of combined view/projection matrix (16 floats). 0 = auto-scan.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_camera_view_addr, 0, "Beautiful Katamari",
    "Guest address of view matrix (16 floats). 0 = identity.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_camera_proj_addr, 0, "Beautiful Katamari",
    "Guest address of projection matrix (16 floats). 0 = identity.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_camera_scan_start, 0x80000000, "Beautiful Katamari",
    "Guest memory start address for camera matrix auto-scan.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(bk_native_camera_scan_end, 0x88000000, "Beautiful Katamari",
    "Guest memory end address for camera matrix auto-scan.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

inline uint32_t MeshCtxVbAddr() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_vb_addr"); }
inline uint32_t MeshCtxVbBytes() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_vb_bytes"); }
inline uint32_t MeshCtxIbAddr() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_ib_addr"); }
inline uint32_t MeshCtxIbCount() { return rex::cvar::Query<uint32_t>("bk_native_mesh_ctx_ib_count"); }
inline uint32_t CameraViewProjAddr() { return rex::cvar::Query<uint32_t>("bk_native_camera_view_proj_addr"); }
inline uint32_t CameraViewAddr() { return rex::cvar::Query<uint32_t>("bk_native_camera_view_addr"); }
inline uint32_t CameraProjAddr() { return rex::cvar::Query<uint32_t>("bk_native_camera_proj_addr"); }
inline uint32_t CameraScanStart() { return rex::cvar::Query<uint32_t>("bk_native_camera_scan_start"); }
inline uint32_t CameraScanEnd() { return rex::cvar::Query<uint32_t>("bk_native_camera_scan_end"); }

// Scan guest memory for plausible camera matrices.
// Looks for 16 consecutive floats that look like a view/projection matrix.
// Returns the first address found, 0 otherwise.
// Logs all candidates found.
uint32_t ScanCameraMatrix(uint8_t* guest_base, uint32_t start_addr, uint32_t end_addr) {
  int candidates = 0;
  uint32_t best_addr = 0;
  float best_score = 0.0f;

  for (uint32_t addr = start_addr; addr < end_addr - 64; addr += 4) {
    float m[16];
    bool ok = true;
    bool effectively_zero = true;
    for (int i = 0; i < 16; i++) {
      uint32_t raw = 0;
      if (!GuestTryLoadU32(guest_base, addr + i * 4, &raw)) { ok = false; break; }
      uint32_t swapped = ((raw >> 24) & 0xFF) | ((raw >> 8) & 0xFF00) |
                         ((raw << 8) & 0xFF0000) | ((raw << 24) & 0xFF000000);
      m[i] = *reinterpret_cast<float*>(&swapped);
      if (!std::isfinite(m[i]) || std::fabs(m[i]) > 100000.0f) { ok = false; break; }
      if (std::fabs(m[i]) > 0.0001f) effectively_zero = false;
    }
    if (!ok || effectively_zero) continue;

    // Reject identity matrix (not a camera)
    bool is_identity = true;
    for (int i = 0; i < 16; i++) {
      float expected = (i % 5 == 0) ? 1.0f : 0.0f;
      if (std::fabs(m[i] - expected) > 0.001f) { is_identity = false; break; }
    }
    if (is_identity) continue;

    // Score: projection matrices typically have specific structure
    float score = 0.0f;
    if (std::fabs(m[15] - 1.0f) < 0.1f) score += 1.0f;
    if (std::fabs(m[12]) < 1000.0f && std::fabs(m[13]) < 1000.0f && std::fabs(m[14]) < 1000.0f)
      score += 0.5f;
    float max_val = 0.0f;
    for (int i = 0; i < 16; i++) max_val = std::max(max_val, std::fabs(m[i]));
    if (max_val > 0.001f && max_val < 100.0f) score += 0.5f;

    candidates++;
    if (score > best_score) {
      best_score = score;
      best_addr = addr;
    }
    if (candidates <= 10) {
      fprintf(stderr, "=== bk native-render: Camera candidate at 0x%08X score=%.1f: %f %f %f %f ...\n",
              addr, score, m[0], m[1], m[2], m[3]);
    }
  }
  if (candidates > 0) {
    fprintf(stderr, "=== bk native-render: Camera scan found %d candidates, best=0x%08X score=%.1f ===\n",
            candidates, best_addr, best_score);
    return best_addr;
  }
  fprintf(stderr, "=== bk native-render: Camera scan found no candidates ===\n");
  return 0;
}

bool ReadCtxU32(uint8_t* guest_base, uint32_t ctx, uint32_t offset,
                uint32_t* out) {
  return GuestTryLoadU32(guest_base, ctx + offset, out);
}

// Read a 4x4 matrix (16 floats) from guest memory.
// Returns true if all 16 floats read successfully.
// Guest is big-endian; converts to host float.
bool ReadMatrix4x4(uint8_t* guest_base, uint32_t addr, float out[16]) {
  if (addr == 0) return false;
  for (int i = 0; i < 16; i++) {
    uint32_t raw = 0;
    if (!GuestTryLoadU32(guest_base, addr + i * 4, &raw)) return false;
    uint32_t swapped = ((raw >> 24) & 0xFF) | ((raw >> 8) & 0xFF00) |
                       ((raw << 8) & 0xFF0000) | ((raw << 24) & 0xFF000000);
    out[i] = *reinterpret_cast<float*>(&swapped);
  }
  return true;
}

// Build a simple orthographic projection matrix (better for UI / 2D elements)
void BuildPerspectiveProjection(float out[16]) {
  float aspect = 1280.0f / 720.0f;
  float left = -aspect;
  float right = aspect;
  float bottom = -1.0f;
  float top = 1.0f;
  float near = 0.1f;
  float far = 100.0f;
  for (int i = 0; i < 16; i++) out[i] = 0.0f;
  out[0] = 2.0f / (right - left);
  out[5] = 2.0f / (top - bottom);
  out[10] = -2.0f / (far - near);
  out[12] = -(right + left) / (right - left);
  out[13] = -(top + bottom) / (top - bottom);
  out[14] = -(far + near) / (far - near);
  out[15] = 1.0f;
}

bool PlausibleVB(uint32_t addr, uint32_t bytes) {
  return addr != 0 && bytes > 0 && bytes < (16u << 20);
}

bool PlausibleIB(uint32_t addr, uint32_t count) {
  return addr != 0 && count > 0 && count < (100u << 10); // max 100K indices per item
}

bool ResolveSceneDrawItem(uint8_t* guest_base, DrawItem& item) {
  // Already resolved
  if (item.vb_data && item.vb_bytes > 0) return true;

  uint32_t vb_addr = 0, vb_bytes = 0, ib_addr = 0, ib_count = 0;
  bool vb_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxVbAddr(), &vb_addr);
  bool vb_bytes_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxVbBytes(), &vb_bytes);
  bool ib_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxIbAddr(), &ib_addr);
  bool ib_count_ok = ReadCtxU32(guest_base, item.ctx, MeshCtxIbCount(), &ib_count);

  // Debug: log raw reads first 5 times
  static int g_read_debug = 0;
  if (g_read_debug < 5) {
    fprintf(stderr, "=== bk native-render: RAW ctx=0x%08X vb@0x%X=0x%08X vb_bytes@0x%X=%u ib@0x%X=0x%08X ib_count@0x%X=%u ===\n",
            item.ctx,
            MeshCtxVbAddr(), vb_addr,
            MeshCtxVbBytes(), vb_bytes,
            MeshCtxIbAddr(), ib_addr,
            MeshCtxIbCount(), ib_count);
    g_read_debug++;
  }

  if (!vb_ok || !vb_bytes_ok || !ib_ok || !ib_count_ok) {
    return false; // guest memory fault, try again later
  }

  if (!PlausibleVB(vb_addr, vb_bytes)) {
    static int g_impl_count = 0;
    if (g_impl_count < 10) {
      fprintf(stderr, "=== bk native-render: IMPLausible VB ctx=0x%08X vb_addr=0x%08X vb_bytes=%u ===\n",
              item.ctx, vb_addr, vb_bytes);
      g_impl_count++;
    }
    return false; // not a valid vertex buffer yet
  }

  item.vb_addr = vb_addr;
  item.ib_addr = ib_addr;
  item.vb_bytes = vb_bytes;
  item.ib_count = ib_count;
  item.vb_data = guest_base + vb_addr;
  item.ib_data = guest_base + ib_addr;

  static int g_debug_count = 0;
  if (g_debug_count < 10) {
    fprintf(stderr, "=== bk native-render: VB RESOLVED ctx=0x%08X vb_addr=0x%08X vb_bytes=%u ib_addr=0x%08X ib_count=%u ===\n",
                item.ctx, vb_addr, vb_bytes, ib_addr, ib_count);
    g_debug_count++;
  }
  return true;
}

}  // namespace

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx, bool world_path,
                             bool drew_inside) {
  if (!ctx) return 0;

  static int capture_count = 0;
  REXLOG_INFO("bk native-render: CaptureDynamicState ctx=0x%08X (#%d)",
              ctx, ++capture_count);

  DrawItem item;
  item.ctx = ctx;
  item.pending = true;

  g_scene.items.push_back(std::move(item));
  return 1;
}

uint32_t CaptureDynamicState(uint8_t* base, uint32_t ctx) {
  return CaptureDynamicState(base, ctx, true, false);
}

void OnFrameEnd(uint8_t* base, uint64_t frame_index,
                const std::vector<DrawItem>& items) {
  static int g_frame = 0;
  static auto g_last_fps_time = std::chrono::steady_clock::now();
  static int g_frames_since_fps = 0;

  g_frame++;
  g_frames_since_fps++;

  std::lock_guard<std::mutex> lock(g_build_mutex);

  FrameScene scene;
  scene.generation = ++g_generation;

  // Read view/projection matrix from guest memory (camera extraction)
  uint32_t vp_addr = CameraViewProjAddr();
  uint32_t view_addr = CameraViewAddr();
  uint32_t proj_addr = CameraProjAddr();

  // Auto-scan for camera matrix if no addresses configured
  static bool g_camera_scanned = false;
  if (vp_addr == 0 && view_addr == 0 && proj_addr == 0 && !g_camera_scanned) {
    g_camera_scanned = true;
    uint32_t scanned = ScanCameraMatrix(base, CameraScanStart(), CameraScanEnd());
    if (scanned != 0) {
      fprintf(stderr, "=== bk native-render: Auto-scanned camera matrix at 0x%08X ===\n", scanned);
      vp_addr = scanned;
    } else {
      fprintf(stderr, "=== bk native-render: Camera matrix auto-scan found nothing ===\n");
    }
  }

  static bool g_camera_logged = false;
  if (!g_camera_logged && (vp_addr != 0 || view_addr != 0 || proj_addr != 0)) {
    g_camera_logged = true;
    fprintf(stderr, "=== bk native-render: Camera addresses vp=0x%08X view=0x%08X proj=0x%08X ===\n",
            vp_addr, view_addr, proj_addr);
  }

  if (vp_addr != 0) {
    if (ReadMatrix4x4(base, vp_addr, scene.view_proj)) {
      // Successfully read combined view/proj
      // Check if matrix is valid (not all zeros)
      bool all_zero = true;
      for (int i = 0; i < 16; i++) {
        if (std::fabs(scene.view_proj[i]) > 0.0001f) { all_zero = false; break; }
      }
      if (all_zero) BuildPerspectiveProjection(scene.view_proj);
    } else {
      BuildPerspectiveProjection(scene.view_proj);
    }
  } else if (view_addr != 0 && proj_addr != 0) {
    float view[16], proj[16];
    if (ReadMatrix4x4(base, view_addr, view) &&
        ReadMatrix4x4(base, proj_addr, proj)) {
      // Multiply proj * view (column-major)
      for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
          scene.view_proj[row * 4 + col] = 0.0f;
          for (int k = 0; k < 4; k++)
            scene.view_proj[row * 4 + col] +=
                proj[row * 4 + k] * view[k * 4 + col];
        }
      }
    } else {
      BuildPerspectiveProjection(scene.view_proj);
    }
  } else {
    BuildPerspectiveProjection(scene.view_proj);
  }
  scene.cam_pos[0] = scene.cam_pos[1] = scene.cam_pos[2] = 0.0f;

  // Resolve each item (read guest memory for VB/IB pointers).
  // Items that fail resolution are dropped for this frame.
  int resolved = 0;
  int failed_ctx[16] = {0};
  int failed_count = 0;
  for (const auto& src : items) {
    DrawItem item = src; // mutable copy
    if (ResolveSceneDrawItem(base, item)) {
      scene.items.push_back(std::move(item));
      resolved++;
    } else if (failed_count < 8) {
      failed_ctx[failed_count++] = item.ctx;
    }
  }

  g_scene = std::move(scene);

  // FPS report every second (after resolution)
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_fps_time);
  if (elapsed >= std::chrono::milliseconds(1000)) {
    double fps = g_frames_since_fps * 1000.0 / elapsed.count();
    fprintf(stderr, "=== bk native-render: FPS=%.1f frame=%d items=%zu resolved=%d/%zu ===\n",
            fps, g_frame, items.size(), resolved, items.size());
    g_last_fps_time = now;
    g_frames_since_fps = 0;
  }

  // Log unresolved contexts once per second
  static int g_unresolved_frame = 0;
  if (failed_count > 0 && (g_frame - g_unresolved_frame) >= 60) {
    g_unresolved_frame = g_frame;
    fprintf(stderr, "=== bk native-render: UNRESOLVED contexts (frame %d) ===\n", g_frame);
    for (int i = 0; i < failed_count; i++) {
      fprintf(stderr, "  ctx=0x%08X (item kind=%u)\n", failed_ctx[i], 0);
    }
    fprintf(stderr, "=== bk native-render: total items=%zu resolved=%d failed=%d ===\n",
            items.size(), resolved, failed_count);
  }

  // Debug: log IB count info on first frame
  static bool g_ib_debug = false;
  if (!g_ib_debug && resolved > 0) {
    g_ib_debug = true;
    fprintf(stderr, "=== bk native-render: IB count debug ===\n");
    for (const auto& src : items) {
      if (src.ib_count > 0 && src.ib_count < 1000000) {
        fprintf(stderr, "  ctx=0x%08X ib_count=%u ib_addr=0x%08X vb_bytes=%u\n",
                src.ctx, src.ib_count, src.ib_addr, src.vb_bytes);
      }
    }
  }
}

uint64_t DrawSequence() { return g_draw_sequence; }

FrameScene& CurrentScene() { return g_scene; }
}  // namespace bk::native
