// beautiful_katamari - native renderer bone palette / world matrix capture.
//
// Captures the per-draw transform state (bone palette for skinned meshes,
// world matrix for rigid model-space props) from the guest constant bank
// after the guest's own draw functions have flushed their constants.
// Modeled on skate3recomp's native/skate3_native_palette.h but rewritten for
// Beautiful Katamari's guest render pipeline.

#pragma once

#include <cstdint>
#include <vector>

#include "bk_native_guest_read.h"

namespace bk::native {

// Capture the bone palette for a skinned mesh from the guest VS constant
// bank. bank = the guest constant bank address; pb = the starting register
// (3 float4 rows per bone, column-vector affine [R|t]). rows_floats = number
// of floats available in the palette. Returns the number of rows copied into
// out (3 floats per bone row).
//
// The guest staging bank is reused draw-to-draw, so we copy immediately at
// capture time rather than deferring.
uint32_t CaptureBonePalette(uint8_t* base, uint32_t bank, uint32_t pb,
                            std::vector<float>& out);

// Capture the world matrix (3 rows, translation in component 3) from the
// guest constant bank. bank/pb as above. Returns true on success.
bool CaptureWorldMatrix(uint8_t* base, uint32_t bank, uint32_t pb, float out_rows[12]);

// Serve a rigid world matrix from an owning entity's guest fields when the
// draw-time constant bank read refused (foreign bank, post-shadow tail
// clobber, torn state). Looks up the entity that owns ctx and reads its
// world block. Returns true on success.
bool ServeEntityWorldMatrix(uint8_t* base, uint32_t ctx, uint32_t vb_obj,
                            float out_rows[12]);

// Fade / opacity serve for a draw item: read the owning entity's alpha
// field. Returns false when unmapped or unreadable, leaving the caller's
// capture untouched.
bool ReadItemFade(uint8_t* base, uint32_t ctx, float* out_alpha);

}  // namespace bk::native
