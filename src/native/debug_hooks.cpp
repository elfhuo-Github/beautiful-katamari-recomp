#include "bk_native_render.h"
#include "bk_native_scene.h"
#include "bk_native_guest_read.h"
#include "bk_native_scene_gpu.h"

#include <rex/cvar.h>
#include <rex/logging.h>

namespace bk::native_render {
namespace {

using VdSwapFn = void (*)(PPCContext&, uint8_t*);
static VdSwapFn g_real_vd_swap = nullptr;
static bool g_vd_swap_resolved = false;

using SubFunc = void (*)(PPCContext&, uint8_t*);
static SubFunc g_real_sub_824DF5F8 = nullptr;
static bool sub_824DF5F8_resolved = false;

static thread_local bool g_in_hook = false;
static int vdswap_call_count = 0;

} // namespace

extern "C" void __imp__VdSwap(PPCContext& ctx, uint8_t* base) {
    static thread_local bool g_in_hook = false;
    if (g_in_hook) return;
    g_in_hook = true;

    static int call_count = 0;
    call_count++;
    
    REXLOG_INFO("bk native-render: __imp__VdSwap called #%d (r3=0x%08X r4=0x%08X)", 
                call_count, ctx.r3.u32, ctx.r4.u32);

    // Capture draw data from guest
    if (rex::cvar::GetFlagByName("bk_native_render") == "true" &&
        rex::cvar::GetFlagByName("bk_native_render_scene") == "true") {
        // Read guest memory at r3 address
        if (ctx.r3.u32 > 0 && ctx.r3.u32 < 0x100000000) {
            REXLOG_INFO("bk native-render: Guest address 0x%08X, r4=0x%08X", 
                        ctx.r3.u32, ctx.r4.u32);
        }
    }

    // Call original guest function
    if (g_real_vd_swap) {
        g_real_vd_swap(ctx, base);
    }

    g_in_hook = false;
}

} // namespace
PYEOF
echo "Debug hook written"
