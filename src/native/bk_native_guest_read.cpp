// beautiful_katamari - native renderer guest memory access.
// Simple implementation: direct reads from guest memory.
// On Linux with mmap-backed guest memory, raw reads don't fault for mapped pages.

#include "bk_native_guest_read.h"

#include <cstring>
#include <rex/logging.h>

namespace bk::native {

bool GuestTryCopy(void* dst, const void* src, size_t size) {
  memcpy(dst, src, size);
  return true;
}

bool GuestTryLoadU32(uint8_t* guest_base, uint32_t guest_addr, uint32_t* out) {
  if (!guest_base) return false;
  // Guest memory is big-endian
  const uint8_t* p = guest_base + guest_addr;
  *out = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  return true;
}

bool GuestTryLoadU16(uint8_t* guest_base, uint32_t guest_addr, uint16_t* out) {
  if (!guest_base) return false;
  const uint8_t* p = guest_base + guest_addr;
  *out = (uint16_t(p[0]) << 8) | uint16_t(p[1]);
  return true;
}

GuestReadRecoveryScope::GuestReadRecoveryScope(uint8_t* guest_base) {
  (void)guest_base;
}

GuestReadRecoveryScope::~GuestReadRecoveryScope() {}

void ArmGuestReadRecoveryForThread(uint8_t* guest_base) {
  (void)guest_base;
}

uint64_t GuestReadRecoveryCount() { return 0; }

float GuestHalfToFloat(uint16_t h) {
  // IEEE-754 binary16 to float
  int sign = (h >> 15) & 1;
  int exp = (h >> 10) & 0x1F;
  int mant = h & 0x3FF;
  if (exp == 0) {
    if (mant == 0) return sign ? -0.0f : 0.0f;
    // Denormal
    return (sign ? -1.0f : 1.0f) * mant * 5.960464477539063e-8f;
  }
  if (exp == 31) {
    if (mant == 0) return sign ? -INFINITY : INFINITY;
    return NAN;
  }
  return (sign ? -1.0f : 1.0f) * (1.0f + mant / 1024.0f) *
         (1 << (exp - 15));
}

}  // namespace bk::native
