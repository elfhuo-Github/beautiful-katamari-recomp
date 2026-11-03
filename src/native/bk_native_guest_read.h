// beautiful_katamari - native renderer guest memory access.
//
// Safe (fault-guarded) reads of guest memory for the native scene renderer.
// The guest can revoke pages (streaming decommit, arena recycling) between a
// pointer being captured and the renderer dereferencing it. On Linux a raw
// read of a revoked page is a fatal SIGSEGV/SIGBUS, so every raw guest load
// in the renderer must go through GuestTryCopy / GuestReadRecoveryScope.
//
// Windows: guest memory is pagefile-backed so raw reads never fault; the
// guarded path compiles to a plain memcpy there.
//
// Posix: GuestReadRecoveryScope installs a SIGSEGV handler that recovers read
// faults inside the guest mapping by restoring read access to the faulted
// host page and resuming the interrupted load (which then completes with
// resident-but-meaningless bytes that callers' plausibility gates reject).
// Nestable; scoped to one native-scene entry point on a guest thread so a
// genuine wild read elsewhere still crashes loudly.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <rex/ppc/context.h>
#include <rex/types.h>

namespace bk::native {

// Bulk copy of guest memory with fault recovery.
//   dst  - host destination
//   src  - host pointer = guest_base + guest_addr
//   size - bytes to copy
// Returns false when the range faults. Any guarded guest read in the renderer
// must use this function; see the comment at GuestReadRecoveryScope for why
// open-coded memcpy is unsafe on posix.
bool GuestTryCopy(void* dst, const void* src, size_t size);

// Read a big-endian u32 from guest memory, fault-guarded.
// Returns false on fault.
bool GuestTryLoadU32(uint8_t* guest_base, uint32_t guest_addr, uint32_t* out);

// Read a big-endian u16 from guest memory, fault-guarded.
bool GuestTryLoadU16(uint8_t* guest_base, uint32_t guest_addr, uint16_t* out);

// Arm read-fault recovery for the current thread for one guest-base.
// Call once per native-scene entry point on a guest thread. Nestable.
// On Windows this is a no-op.
class GuestReadRecoveryScope {
 public:
  explicit GuestReadRecoveryScope(uint8_t* guest_base);
  ~GuestReadRecoveryScope();
  GuestReadRecoveryScope(const GuestReadRecoveryScope&) = delete;
  GuestReadRecoveryScope& operator=(const GuestReadRecoveryScope&) = delete;
};

// Permanently arm read-fault recovery for a renderer thread (render thread,
// prewarm decode workers). Idempotent.
void ArmGuestReadRecoveryForThread(uint8_t* guest_base);

// Total recovered reads since startup (telemetry).
uint64_t GuestReadRecoveryCount();

// Convert a guest half-float (IEEE-754 binary16) to host float.
float GuestHalfToFloat(uint16_t h);

}  // namespace bk::native
