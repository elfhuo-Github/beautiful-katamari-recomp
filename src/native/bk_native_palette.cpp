// beautiful_katamari - native renderer bone palette / world matrix capture
// (implementation).

#include "bk_native_palette.h"

#include <cstring>

namespace bk::native {

uint32_t CaptureBonePalette(uint8_t* base, uint32_t bank, uint32_t pb,
                            std::vector<float>& out) {
  // Each bone row is 3 float4 registers = 12 floats. Read register by
  // register from the guest constant bank at base + bank + (pb + i)*4.
  // The guest stores floats big-endian; GuestTryLoadU32 handles bswap.
  constexpr uint32_t kRowsPerBone = 3;
  constexpr uint32_t kFloatsPerRow = 4;

  // Read up to 256 bones (the typical VS constant bank limit).
  uint32_t rows_copied = 0;
  for (uint32_t bone = 0; bone < 256; bone++) {
    uint32_t reg = pb + bone * kRowsPerBone;
    bool any_valid = false;
    for (uint32_t row = 0; row < kRowsPerBone; row++) {
      uint32_t v = 0;
      if (!GuestTryLoadU32(base, bank + reg * 4, &v)) break;
      out.push_back(*reinterpret_cast<float*>(&v));
      reg++;
      any_valid = true;
    }
    if (!any_valid) break;
    rows_copied += kRowsPerBone;
  }
  return rows_copied;
}

bool CaptureWorldMatrix(uint8_t* base, uint32_t bank, uint32_t pb,
                        float out_rows[12]) {
  // World matrix: 3 rows of 4 floats each, translation in component 3.
  // Starts at constant register pb.
  for (uint32_t row = 0; row < 3; row++) {
    for (uint32_t col = 0; col < 4; col++) {
      uint32_t reg = pb + row * 4 + col;
      uint32_t v = 0;
      if (!GuestTryLoadU32(base, bank + reg * 4, &v)) return false;
      out_rows[row * 4 + col] = *reinterpret_cast<float*>(&v);
    }
  }
  // Row 3 is implicitly (0, 0, 0, 1) for affine matrices.
  out_rows[3] = 0.0f;
  out_rows[7] = 0.0f;
  out_rows[11] = 1.0f;
  out_rows[12] = out_rows[13] = out_rows[14] = 0.0f;
  out_rows[15] = 1.0f;
  return true;
}

bool ServeEntityWorldMatrix(uint8_t* base, uint32_t ctx, uint32_t vb_obj,
                            float out_rows[12]) {
  // BK-specific: looks up the entity that owns ctx and reads its world block.
  // Concrete entity layout TBD from reverse engineering of BK's guest code.
  // Returns false (unimplemented) until BK-specific offsets are known.
  (void)base;
  (void)ctx;
  (void)vb_obj;
  (void)out_rows;
  return false;
}

bool ReadItemFade(uint8_t* base, uint32_t ctx, float* out_alpha) {
  // BK-specific: reads the owning entity's alpha field.
  // Concrete layout TBD; returns false until known.
  (void)base;
  (void)ctx;
  (void)out_alpha;
  return false;
}

}  // namespace bk::native
