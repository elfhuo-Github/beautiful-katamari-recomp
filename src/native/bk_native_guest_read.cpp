// beautiful_katamari - native renderer guest memory access (implementation).

#include "bk_native_guest_read.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(__linux__)
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace bk::native {
namespace {

// Guest base pointer for the current thread's recovery scope. Set by
// GuestReadRecoveryScope ctor, cleared by dtor.
thread_local uint8_t* tls_guest_base = nullptr;

// Saved previous SIGSEGV handler, so we can restore it on scope exit.
thread_local struct sigaction tls_old_segv{};
thread_local bool tls_have_old_segv = false;

}  // namespace

#if defined(__linux__)
// Posix read-fault recovery: on SIGSEGV inside the guest mapping, restore
// read access to the faulted page and resume the faulting instruction. The
// load then completes with whatever bytes are now resident (likely zeros or
// stale data), which callers' plausibility gates reject.
static void SegvHandler(int sig, siginfo_t* info, void* uctx) {
  if (sig != SIGSEGV || tls_guest_base == nullptr) {
    // Not our domain: re-raise / let the default handler run.
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
    return;
  }

  ucontext_t* uc = static_cast<ucontext_t*>(uctx);
  (void)uc;

#if defined(__x86_64__)
  void* fault_addr = info->si_addr;
  if (!fault_addr) return;

  // Check if the fault address is inside the guest mapping.
  // The guest virtual base is at host 0x100000000 + guest VA.
  // We approximate: any address >= 0x100000000 is guest.
  uintptr_t fa = reinterpret_cast<uintptr_t>(fault_addr);
  const uintptr_t kGuestBase = 0x100000000ull;
  const uintptr_t kGuestSize = 0x100000000ull;  // 4 GB guest space
  if (fa < kGuestBase || fa >= kGuestBase + kGuestSize) {
    // Outside guest mapping: let it crash.
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
    return;
  }

  // Make the faulted page readable (copy-on-write pages may have been
  // revoked by the guest). Map it read-only.
  const long page_size = sysconf(_SC_PAGE_SIZE);
  uintptr_t page_start = fa & ~(uintptr_t)(page_size - 1);
  if (mprotect(reinterpret_cast<void*>(page_start), (size_t)page_size,
               PROT_READ) == 0) {
    // Resume the faulting instruction: it will re-read and now succeed.
    return;
  }
#endif  // __x86_64__

  // If mprotect failed or not x86-64, we can't recover: crash.
  signal(SIGSEGV, SIG_DFL);
  raise(SIGSEGV);
}
#endif  // __linux__

//------------------------------------------------------------------------------
// GuestTryCopy
//------------------------------------------------------------------------------

bool GuestTryCopy(void* dst, const void* src, size_t size) {
  if (size == 0) return true;
  if (!dst || !src) return false;

#if defined(__linux__)
  // On posix, install a SIGSEGV handler so a page fault is recovered rather
  // than crashing. The handler resumes the faulting instruction in place, so
  // the copy completes (possibly with garbage that callers' plausibility
  // gates reject) or re-raises.
  struct sigaction sa, old_sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = SegvHandler;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);

  struct sigaction old_buf;
  bool installed = (sigaction(SIGSEGV, &sa, &old_buf) == 0);

  std::memcpy(dst, src, size);

  if (installed) sigaction(SIGSEGV, &old_buf, nullptr);
#else
  // Windows: guest memory is pagefile-backed, raw reads never fault.
  std::memcpy(dst, src, size);
#endif
  return true;
}

//------------------------------------------------------------------------------
// GuestTryLoadU32 / GuestTryLoadU16
//------------------------------------------------------------------------------

bool GuestTryLoadU32(uint8_t* guest_base, uint32_t guest_addr, uint32_t* out) {
  if (!out || !guest_base) return false;
  uint32_t v = 0;
  if (!GuestTryCopy(&v, guest_base + guest_addr, 4)) return false;
  *out = __builtin_bswap32(v);
  return true;
}

bool GuestTryLoadU16(uint8_t* guest_base, uint32_t guest_addr, uint16_t* out) {
  if (!out || !guest_base) return false;
  uint16_t v = 0;
  if (!GuestTryCopy(&v, guest_base + guest_addr, 2)) return false;
  *out = __builtin_bswap16(v);
  return true;
}

//------------------------------------------------------------------------------
// GuestReadRecoveryScope
//------------------------------------------------------------------------------

#if defined(__linux__)
GuestReadRecoveryScope::GuestReadRecoveryScope(uint8_t* guest_base) {
  tls_guest_base = guest_base;
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = SegvHandler;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  // Save the previous handler so we can restore it on exit.
  tls_have_old_segv = (sigaction(SIGSEGV, &sa, &tls_old_segv) == 0);
}

GuestReadRecoveryScope::~GuestReadRecoveryScope() {
  tls_guest_base = nullptr;
  // Restore the previous SIGSEGV handler instead of unconditionally resetting
  // to SIG_DFL. The game may have its own handler that we must not clobber.
  if (tls_have_old_segv) {
    sigaction(SIGSEGV, &tls_old_segv, nullptr);
    tls_have_old_segv = false;
  } else {
    signal(SIGSEGV, SIG_DFL);
  }
}
#else
GuestReadRecoveryScope::GuestReadRecoveryScope(uint8_t* /*guest_base*/) {}
GuestReadRecoveryScope::~GuestReadRecoveryScope() {}
#endif

void ArmGuestReadRecoveryForThread(uint8_t* /*guest_base*/) {
#if defined(__linux__)
  static bool armed = false;
  if (!armed) {
    armed = true;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = SegvHandler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
  }
#endif
}

uint64_t GuestReadRecoveryCount() {
  return 0;
}

//------------------------------------------------------------------------------
// GuestHalfToFloat
//------------------------------------------------------------------------------

float GuestHalfToFloat(uint16_t h) {
  // IEEE-754 binary16 -> float. h is big-endian as stored by the guest;
  // the caller has already bswapped if needed.
  uint32_t sign = (h >> 15) & 1;
  uint32_t exp = (h >> 10) & 0x1F;
  uint32_t mant = h & 0x3FF;

  if (exp == 0) {
    if (mant == 0) return sign ? -0.0f : 0.0f;
    float m = static_cast<float>(mant) / 1024.0f;
    return (sign ? -1.0f : 1.0f) * m * 5.960464477539063e-8f;
  }
  if (exp == 31) {
    if (mant == 0) return sign ? -__builtin_inff() : __builtin_inff();
    return __builtin_nanf("");
  }
  float m = 1.0f + static_cast<float>(mant) / 1024.0f;
  float e;
  if (exp >= 15) {
    e = 1.0f;
    for (int i = 0; i < exp - 15; i++) e *= 2.0f;
  } else {
    e = 1.0f;
    for (int i = 0; i < 15 - exp; i++) e *= 2.0f;
    e = 1.0f / e;
  }
  return (sign ? -1.0f : 1.0f) * m * e;
}

}  // namespace bk::native
