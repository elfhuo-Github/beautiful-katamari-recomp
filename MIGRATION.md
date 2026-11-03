# BKReXGlue — porting BKRecomp onto rexglue-sdk

**Beautiful Katamari** (Xbox 360, Title ID `4E4D07DC`) statically recompiled with the
[rexglue-sdk](https://github.com/) toolkit, **v0.10.0**.

This project is a sibling rewrite of the original `BKRecomp/` project (which used
XenonRecomp plus a hand-written runtime). `BKRecomp/` is left untouched as the
reference; everything here lives under `BKReXGlue/`.

---

## 1. What this is

| | BKRecomp (old) | BKReXGlue (this) |
|---|---|---|
| Recompiler | XenonRecomp (block-based) | rexglue `codegen` (PDATA + branch + RTTI vtable discovery, GapFill) |
| Runtime (kernel/threads/imports) | hand-written (`runtime/*.cpp`) | rexglue runtime library |
| GPU | hand-written swapchain (`runtime/gpu.cpp`, `display.cpp`) | rexglue **xenos** GPU plugin (Vulkan) + presenters |
| App entry | custom `main.cpp` | `rex::ReXApp` subclass (`src/beautiful_katamari_app.h`) |
| Game-specific artifact | block dump + runtime patches | `codegen` config (`bk_*.toml`) |

Only the *codegen configuration* is genuinely game-specific and carried over; the
runtime and GPU work is entirely superseded by the SDK.

### Status (short)

- `codegen` succeeds: **37k+ functions** recovered, no `REX_FATAL("Unresolved …")`
  emitted anywhere in the generated tree.
- Full Release build links a ~75 MB `beautiful_katamari` executable with the
  `xenos` plugin and `librexruntime.so` staged beside it; `ldd` resolves.
- The game **boots and runs**: SDL/Vulkan select the real GPU, a 1920×1080
  swapchain is created, the XEX loads, the kernel initialises, GPU/shader storage
  comes up, audio endpoints are opened, and the game reaches rendering — it
  **creates real graphics pipeline states** (VS/PS shader hashes) on the GPU.
- It then runs the entire 150 s test window with **no fatal error** and shuts down
  cleanly. The automated recovery loop that drives out `codegen`-coverage gaps
  (see §4/§7) **converged**: a full run surfaced no further
  `Call to invalid or unregistered function`.

---

## 2. Layout

```
BKReXGlue/
├── beautiful_katamari_manifest.toml   # rexglue project manifest (entrypoint + includes)
├── bk_config.toml                     # manual function-boundary overrides (the port artifact)
├── bk_gapfill_thunks.toml             # auto-recovered GapFill-swallowed functions
├── bk_switch_tables.toml              # REFERENCE ONLY - see §4 (not wired in)
├── CMakeLists.txt                     # host target + xenos plugin + runtime staging
├── CMakePresets.json                  # stock rexglue presets
├── src/
│   ├── main.cpp                       # REX_DEFINE_APP
│   └── beautiful_katamari_app.h       # ReXApp subclass (extension points)
├── generated/                         # rexglue codegen output (not hand-edited)
└── out/build/linux-amd64-release/     # build tree (executable, plugins, logs/)
```

---

## 3. Build & run

Prerequisites: `cmake >= 3.25`, `ninja`, `clang-18`, the rexglue SDK.

The stock `linux-amd64-release` preset requests `clang-20`; on this machine only
`clang-18` exists, so configure with explicit compilers:

```sh
cmake -S . -B out/build/linux-amd64-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
      -DCMAKE_PREFIX_PATH=/tmp/opencode/sdk/linux-amd64
cmake --build out/build/linux-amd64-release -j3
```

`codegen` runs automatically as part of the build (a `add_custom_command` with a
depfile covering the manifest, the XEX, and the included `bk_*.toml` files), so
editing any config file re-runs it.

Run (the game data root is a cvar; it is required):

```sh
cd out/build/linux-amd64-release
./beautiful_katamari --game_data_root="/path/to/Beautiful Katamari (USA) (En,Ja)"
```

Detailed logs are written per run to `out/build/linux-amd64-release/logs/*.log`
(the console only echoes a few lines).

Build notes:
- Each large generated translation unit needs ~325 MB RSS, so `-j3` is a good
  ceiling on a ~32 GB machine.
- The SDK does **not** stage `librexruntime.so` on Linux; `CMakeLists.txt` adds a
  `POST_BUILD` copy next to the executable so the `$ORIGIN` runpath resolves it.

---

## 4. Codegen configuration — the actual port

rexglue recovers functions from `.pdata`, direct branch targets and an RTTI vtable
scan, then fills the remaining code regions ("GapFill"), merges and validates. It
found **37,342 functions** unaided. The config files only *correct* it.

### `bk_config.toml`

Thin manual layer. It carries:
- `includes = ["bk_gapfill_thunks.toml"]`.
- A handful of explicit `[functions]` entries for boundaries the automatic
  analysis could not associate with a function (they are reached only by tail
  branches / comma-operator stubs / vtable thunks and are absent from PDATA):
  `0x82408488`, `0x825387E8`, `0x8266DEA0`, `0x826735E8`, `0x82685520`,
  `0x822AC5A8`. Sizes come from the previous XenonRecomp build.

### Switch tables: why the old tables are *not* used

The original `BKRecomp/config/BK_switch_tables.toml` (generated by XenonAnalyse)
was converted to rexglue's `[[switch_tables]]` format, but is **deliberately not
wired in** — `bk_switch_tables.toml` is kept as documentation only.

Reason: the two formats key the table differently.

- XenonAnalyse `base` = the address of the first `lis` in the dispatch pattern.
- rexglue `[[switch_tables]].address` = the address of the **`bctr`** instruction
  (see `function_types.h: bctrAddress`).

A verbatim copy therefore registered every table at the wrong address and did
nothing. rexglue's own `detectJumpTable` already recovers **all 764** tables the
old tool found (669 absolute / 27 computed / 68 offsetted); codegen output with
and without the converted file was byte-identical ("0 written"). The header of
`bk_switch_tables.toml` records this.

### GapFill's blind spot and the thunk workaround

GapFill (`src/codegen/phase_gapfill.cpp`) splits an uncovered code region only on
`blr` and on direct branches to already-known callables. Two consequences show up
as runtime `Call to invalid or unregistered function` fatals
(`function_dispatcher.cpp`):

**Class A — thunk runs ending in `bctr`.** A run of consecutive leaf thunks that
each end in an *indirect* terminator (`bctr`, e.g. virtual-dispatch thunks
`lwz r11,0(r3); lwz r11,N(r11); mtctr r11; bctr`) is not split at all, so it is
registered as a single gap-fill function. Only the first thunk gets code; the
rest have no entry. `bk_gapfill_thunks.toml` declares the missing starts; the
initial 128 were recovered by cross-referencing the previous XenonRecomp build's
block starts (see `tools/` notes below).

**Class B — PDATA ends swallowed by a mis-anchored segment.** When GapFill splits
at a tail call *inside* a PDATA function, the resulting segment can start inside
that function's range. Because `gapFillCodeRegions` skips any segment whose start
is already *inside* another function, the whole segment is dropped — including the
part after the PDATA end, where the next real function lives.

Both classes are worked around by declaring bare `[functions]` entries in
`bk_gapfill_thunks.toml`:

```toml
[functions]
"0x826A24F0" = {}        # start only; rexglue discovers the extent itself
```

A size-less entry is enough: with no `size`/`end`, discovery bounds the function
by the containing region and stops at the first terminator, so the correct extent
is found automatically. This is what the automated recovery loop appends.

> These entries only encode addresses the running game actually called. They are a
> workaround for the GapFill blind spot, **not** a complete static recovery; a
> proper fix belongs upstream in `phase_gapfill.cpp` (split on indirect
> terminators, and clamp a skipped segment's start to the containing function's
> end instead of dropping it).

#### Recovery result

`bk_gapfill_thunks.toml` now holds **133** size-declared entries: the 128
statically recovered class-A thunks (with sizes from the old build) plus **5**
further entries appended by the runtime-driven loop
(`0x826A24F0`, `0x8241F428`, `0x82539188`, `0x824681B0`, `0x82638748`), each a
bare `{}` entry. After the fifth, a complete 150 s run surfaced no further
missing function, so the loop exited by convergence (see §7).

---

## 5. What was superseded, and what was ported

Every source file of the old hand-written runtime maps onto an SDK facility:

| `BKRecomp/runtime/…` | BKReXGlue |
|---|---|
| `main.cpp` (custom emulator loop) | `rex::ReXApp` + `REX_DEFINE_APP` |
| `kernel.cpp` | rexglue kernel state |
| `threads.cpp` (wait/event/thread modelling) | rexglue kernel + thread scheduler |
| `imports_impl.cpp`, `import_stubs.cpp` | rexglue `xboxkrnl`/`xam` import resolver |
| `gpu.cpp`, `display.cpp` (SDL swapchain, SCB) | rexglue **xenos** GPU plugin + presenter (Vulkan) |
| `shader.cpp` | rexglue xenos shader translator |
| `config/BK_switch_tables.toml`, `config/BK.toml` | `bk_config.toml` / `bk_switch_tables.toml` (see §4) |

No game-specific *behavioural* patch from the old runtime was needed: the old
fixes (`config/BK.toml`'s `save/restgprlr_14_address`, the jump-table patch in
`ppc_recomp.130.cpp`, the defensive null-guard in `ppc_recomp.29.cpp`) address
symptoms of XenonRecomp-era codegen that rexglue handles itself (save/restore
helper auto-detection, jump-table detection, validation).

### Extension points used

`src/beautiful_katamari_app.h` overrides exactly one hook:

```cpp
void OnPreSetup(rex::RuntimeConfig& config) override {
  if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
}
```

so a plain `beautiful_katamari --game_data_root <dir>` run selects the staged
`librexgpu-xenos.so` without a sidecar config. All other `ReXApp` hooks
(`OnLoadXexImage`, `OnPostSetup`, `OnCreateDialogs`, …) remain the library
defaults and are listed as commented stubs in the header.

`CMakeLists.txt` wires the plugin and the Linux runtime copy:

```cmake
rexglue_setup_target(beautiful_katamari GPU_PLUGINS xenos)
if(UNIX AND NOT APPLE)
  add_custom_command(TARGET beautiful_katamari POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:beautiful_katamari>)
endif()
```

---

## 6. Boot log landmarks

A healthy run reaches, in order:

```
Vulkan … Using "…" with driver: "libGLX_nvidia.so.0"
VulkanPresenter: Created 1920x1080 swapchain …
Guest memory arena mapped: virtual base 0x0000000100000000 …
Function table initialized for module: code=82210000-82A05350, image=82000000-831F0000
Initializing shader storage for title 4E4D07DC…
[NtCreateFile] FAILED: path='D:\drivers.ini' -> 0xc000000f   (harmless)
SetInterruptCallback(824DF228, 40006D00)
audio endpoint '…': 6 ch, 48000 Hz, format 0x8120; submitting 6 ch
Creating graphics pipeline state with VS 0A6D1DD7767FDF27
Creating graphics pipeline state with VS 0A6D1DD7767FDF27, PS 2E372EA28CC404B7
Creating graphics pipeline state with VS 9A8D6A3582291C7B, PS 1E70EB9513D670C9
…
Window closing, shutting down…
Title terminated; hard-exiting process.
```

The final block is the test window's timeout firing: the game had been running
(the `Window closing …` / `Title terminated` pair is normal shutdown), not
aborted. Earlier in the port this point was unreachable — execution died on
`0x82689770` and then on a chain of five more unregistered indirect entry points;
those are now covered by `bk_gapfill_thunks.toml` (see §4).

---

## 7. Reproducing the recovery

The tooling used to build `bk_gapfill_thunks.toml` and to drive the loop lives in
the scratch notes and is described here so it can be re-created:

1. `find_thunks3.py` — parses the previous XenonRecomp build's per-block
   disassembly (`BKRecomp/ppc/ppc_recomp.*.cpp`) into an address→mnemonic map,
   compares its block starts against the current rexglue function set
   (`generated/default/codegen.partition.json`), and reports old block starts that
   follow a `bctr` inside a GapFill segment (class A) or PDATA ends left
   uncovered (class B).
2. `whack.py` — build → run with a timeout → read the newest
   `logs/*.log`, and on `invalid or unregistered function at guest address 0x…`
   append `"0x…" = {}` to `bk_gapfill_thunks.toml`, then repeat. It stops early
   (`DONE (no more missing functions detected)`) when a full run completes with no
   such fatal, and otherwise after the iteration cap. In this port that took six
   iterations (~9 min total) for the five extra entries; iteration 6 ran a clean
   150 s window.

---

## 8. Known limitations

- Boot was **codegen-coverage limited**, not runtime-limited: each un-recovered
  indirect entry point aborted with the invalid-function fatal. The recovery loop
  has since converged for the exercised boot path, but because the list in
  `bk_gapfill_thunks.toml` is populated by *running* the game, deeper code paths
  (menus, gameplay, per-level content) may still reveal further indirect entry
  points. It is a workaround for a fixed boot path, not a proof of full coverage.
- The game reaches **rendering** (real Vulkan pipeline states for its VS/PS
  shaders) but the test window is closed by timeout rather than by gameplay input;
  the first frames are skipped with `Skipping Vulkan frame presentation due to
  async placeholder draw usage in this frame`, and no input/state has been
  meaningfully exercised. "Boots and runs" means it survives and renders, not that
  it is confirmed playable.
- The upstream GapFill fix (split on `bctr`/indirect terminators; clamp skipped
  segments) is not applied here because the prebuilt SDK v0.10.0 was used rather
  than rebuilding the toolchain.

---

## 9. Playability chase (post-boot)

With the boot path converged, the next goal was to get past the stall-after-first
frame. Two rexglue codegen defects were found and worked around via config, and a
third, deeper fault remains open.

### 9.1 Jump-table base/index swap (fixed)

Symptom: the main guest thread (`Main XThread`) hit a `SIGILL` (`__builtin_trap`)
on the very first frame, and the process survived standalone only because rexglue
installs its own signal handlers. Root cause is a rexglue `function_scanner.cpp`
bug: for `lwzx rD, rA, rB` it hard-codes the jump-table **index** register to
`RB`. When the compiler emits the index in `RA` and the (page-aligned, additive)
base in `RB` — `lwzx r11, r10, r11` — the emitted `switch` keys on the *loaded code
address* (`ctx.r11`) instead of the scaled index (`ctx.r10`), so every case misses
and falls through to `default: __builtin_trap()`.

Only 2 of 768 emitted switches are affected (verified by
`tools/scan_switches.py`, which flags any `switch (ctx.rN)` where `rN` was
immediately loaded by an `lwzx` in the preceding lines). Worked around with
per-`bctr` `[[switch_tables]]` entries in `bk_jumptable_fix.toml`, keyed on
`register = 10` (the register holding `idx*4` at the branch), with label lists
padded with `0` at non-multiple-of-4 positions (0 → `__builtin_trap()`, never
taken):

| bctr addr   | function        | labels (real targets at idx 0/4/8/12) |
|-------------|-----------------|----------------------------------------|
| `0x8222E454`| `sub_8222E438`  | 13 entries, targets `…E460/E530/E578/E530` |
| `0x822DF3C4`| `sub_822DF3B0`  | 5 entries, target `…DF470` |

The bctr address must be derived by disassembling the generated code (or the
guest `bctr`), **not** by hand arithmetic — a wrong address that happens to hit a
real function entry silently does nothing. Confirmed fixed: `recomp.23.cpp` and
`recomp.103.cpp` now emit `switch (ctx.r10.u32)` with correct cases.

Effect: the stall-after-first-frame and the SIGILL are gone. The main thread now
runs at ~97% CPU and survives ~18 s (full Vulkan pipeline setup) before the next
fault. Frame #10 of the faulting backtrace is `sub_8222E460`, i.e. case 0 of the
repaired table — the fix is on the executed path.

### 9.2 Store to an out-of-range guest pointer (resolved)

Resolved by the second round of jump-table fixes: two more mis-detected
`lwzx`-based switches (`bk_jumptable_fix.toml`, base `0x82A196A8` with 5 cases
and base `0x82A3B848` with 29) plus the ten MSVC adjustor thunks at
`0x8253E9F8`…`0x82540C70`. With those in place the main thread ran a 124 s
soak (5890 presents, 98.3 s at 59.9 fps) with no fault. The original analysis
of the symptom is kept below for reference.

Next blocker, reached deterministically at ~16–18 s on `Main XThread`:

- `SIGSEGV` at `sub_824DB210` (`generated/…recomp.87.cpp:12484`),
  instruction `REX_STORE_U32(ea, ctx.r9.u32)` where `ea = 4 + ctx.r11.u32`
  (guest `stwu r9,4(r11)`), storing `0xA31` (2609, big-endian → `0x310A0000`).
- `si_addr = 0x1afcea9e0` = guest arena base `0x100000000` + guest EA
  `0xafcea9e0`. The guest EA `r11 = 0xafcea9dc` (~2.95 GB) is far past the end of
  the mapped guest image (`82000000–831F0000`) and past physical RAM, so this is
  an **uninitialized/garbage pointer** being followed as a block base, not a
  legitimate-but-unmapped address.
- Same symptom class as an earlier, pre-fix fault (`sub_824E0110`, store to
  `r11+4`), so likely one underlying pool/linked-list “next pointer” that is
  garbage, reached from the allocator `sub_824DF8C8` → `sub_824DF5F8` →
  `sub_824E0298` chain. No unimplemented-export warning accompanies it (only the
  benign `D:\drivers.ini` `NtCreateFile` misses), so it is not a missing kernel
  stub. Not yet determined whether the bad pointer is a genuine guest read of
  uninitialized memory or a further recompiler miscompilation.

### 9.3 Audio: constant buzz from an empty XMA loop (fixed)

Symptom: after the first playable boot the game produced a constant buzzy tone
instead of music. The XMA decoder was running, but every decode restarted from
the same place.

Root cause: the guest programs each `XMA_CONTEXT_DATA` (64 bytes, big-endian)
directly in guest memory. This game writes `loop_count = 255` ("loop forever")
while leaving `loop_start`/`loop_end` at 0. The runtime clamps a zero
`loop_end` to 32 bits and then treats **every** decode as a loop-end frame: it
resets `input_buffer_read_offset` to 32 after each decode and only emits
`loop_subframe_end + 1` (one) 128-sample subframe. Observed context:
`d0 = 0x07DFF008` (`packet_count = 8`, `loop_count = 255`, `buf0_valid`,
`obc = 31`), `d1 = 0x90800000`, read offset `d2` stuck at 32 forever.

Evidence: clearing `loop_count` in gdb at `XmaContext::Work` entry made the read
offset advance `32 → 4522 → 7052 → … → 127604`, reset to 32 only at the next
input-buffer swap. The output ring then contained 31 distinct blocks of real
audio (rms 10703, peak 32768) and matched a system-ffmpeg decode of
`d02.dat` at 0.711 s with correlation **1.000**. 73 context snapshots
(frames 900–2700) never saw the guest write a non-zero loop region, and a
memory-write watcher on the loop fields never fired. Zero-length loops are
ignored by real hardware, so the runtime's interpretation is what differs.

Fix: app-level workaround in `src/audio_fix.cpp` (the prebuilt SDK library
cannot be rebuilt):

- override `__imp__XMACreateContext` to learn each context's guest virtual
  address (the game creates `0xFFCA6000`, `0xFFCA6040`, … 32 × 64 bytes);
- override `__imp__XAudioSubmitRenderDriverFrame` to patch the known contexts
  inline before submitting a frame;
- a 1 ms detached patcher thread catches contexts that start later;
- patch rule: only when `loop_count != 0 && loop_end == 0`, clear the
  `loop_count` field with a single aligned 32-bit big-endian store (no
  tearing against the runtime's reads).

Verification: `BK_AUDIO_CAPTURE=<path>` (diagnostic env var) dumped every
submitted render frame (6 ch × 256 big-endian floats at 48 kHz, 187.5 frames/s).
A 25 s run yielded 4438 frames. Active runs: 947–1380 (2.31 s, rms 0.042, 83 %
of energy in 300 Hz–1 kHz) and 3056–4437 (7.37 s, rms 0.104, bass-heavy).
Frame-boundary jumps are the same magnitude as within-frame deltas (no clicks),
L/R correlation is 1.000, channels 2–5 are silent (the guest submits mono), and
nothing clips. The 8.7 s gap in between is the loading screen. Confirmed by ear:
"the crackle is gone".

Notes for future debugging: guest floats are big-endian; the guest's XMA
contexts live at virtual `0xFFCA6000 + i*0x40` (host `0x1FFCA6000 + …`) and must
be inspected as big-endian words (`ctx+1 &= 0xF0`, `ctx+2 &= 0x0F` to clear
`loop_count`), not as little-endian dwords.

### 9.4 Input: keyboard/mouse controller emulation, and the save-load crash

The runtime already ships `rex::input::mnk::MnkInputDriver`, which maps
keyboard/mouse events to a virtual Xbox 360 pad (an `X_INPUT_STATE` device with
ID `0x4D4E4B00`). It is disabled by default; set the `mnk_mode` cvar to enable
it. Defaults (visible in the in-game Settings → Input/Keybinds page):

| action | default keys |
|---|---|
| A | `Space` / `Semicolon` |
| B | `Backspace` / `Quote` |
| D-pad | arrow keys (`Shift`+arrows in some builds) |
| Start | `Enter` |
| Back | `Tab` |
| triggers | `Q`/`I` and `E`/`O` |
| sticks | `WASD`-style keys, mouse for the right stick |

`mnk_mode = 1` is persisted in `out/build/linux-amd64-release/beautiful_katamari.toml`;
the runtime loads `<executable folder>/<app name>.toml` and the file is plain
`key = value` cvar overrides. Verified: pressing Space on the title screen
advanced to the loading screen, i.e. the guest received A.

The next blocker was reported as "press A to start → freeze while loading
saves". It was not a hang: the process aborted with

```
[critical] [core] [FATAL] Call to invalid or unregistered function at guest address 0x82426988
```

`0x82426988` is a real 0x20-byte tail-call thunk in the old XenonRecomp build,
and a static vtable slot at `0x820D9E44` points at it. GapFill had merged it
into the neighbouring function (only the entry at `0x82426948` was registered),
so loading the table and calling the slot hit the dispatcher's invalid-function
trap. Declaring it in `bk_config.toml` fixes the save/load path.

To find this class of problem systematically, the read-only guest image was
dumped at runtime (the `/dev/shm/xenia_memory_*` backing file is indexed by
guest VA directly, or use host `0x100000000 + VA`), and the data ranges
`0x82000000–0x82210000` and `0x82A05000–0x82A10000` were scanned for runs of
big-endian words that are all functions in the old build's map. 38 addresses
were present in those tables but unregistered in the new build; the clean ones
are now declared in `bk_config.toml` with sizes from the old map.

Caveat: not every such table word is a function entry. MSVC compiler funclets
and shared tails are interior blocks of a larger function; declaring a boundary
there makes the enclosing function branch across a function boundary and codegen
reports `Unresolved b target … (no CallTarget in FunctionNode)`. Those entries
were removed again (`0x824CF290`, `0x82556E24`, `0x827F09D4`, `0x827F11F4`,
`0x828C6CD4`, `0x82826150`, the `0x828ADB74`/`0x828ADBAC`/`0x828AE4C4`/
`0x828AE518`/`0x828AE77C`/`0x828AE7C8` cluster, and `0x827F04EC`/`0x827F0508`);
only entries whose bodies contain no cross-boundary branches are kept.

`tools/uinput_pad.c` (a uinput Xbox 360 pad driven from stdin) remains useful
for testing the SDL path, but SDL's `GetDeviceState` returned an all-zero state
for it on this machine, so keyboard/mouse emulation is the supported route.

### 9.5 Rendering: left black band (fixed) and the results-screen katamari (open)

#### 9.5.1 Black band down the left of the save/load screens (fixed)

Symptom: the "Select storage device" / "Select data to load" screens render
with a full-height black band over exactly the left quarter of the frame
(320 px of a 1280×720 guest frame). The band is *in the guest image*, not the
presenter: with a 1920×720 window the guest image sits at x=320..1599 (letterbox)
and the band is still 320 wide *inside* it, and the whole band scales if the
guest resolution changes. Everything in the band is missing (the flower circle
on the left of the save screen disappears) and elements that cross x=320 are cut
mid-glyph, while the rest of the UI keeps its normal position — so it is a clip /
overwrite at x=320, not a shifted or scaled image. It is stable indefinitely and
disappears while a Yes/No confirmation dialog is up, returning as soon as the
dialog closes.

Ruled out along the way:

- presenter letterboxing / scaling (`present_letterbox`, safe-area cvars) — the
  guest output delivered to the presenter is 1280×720, 1:1 with the window.
- guest video mode (`--video_mode_width/height`) — the band stays at a quarter of
  the frame.
- the bogus overscan reported by the runtime. `VdGetCurrentDisplayInformation`
  fills `display_window_overscan_left/top/right/bottom` with a quarter of the
  display size (`xboxkrnl_video.cpp`, `overscan_x = display_width / 4`), so a
  scissor built from that field would be exactly this 320 px. `src/video_fix.cpp`
  hooks `__imp__VdGetCurrentDisplayInformation` and can zero the fields with
  `BK_VIDEO_NO_OVERSCAN=1` (`BK_VIDEO_LOG=1` dumps the struct) — **zeroing them
  does not change the band**, so the game is not using those fields for it.
- only the left edge is clipped; the top/bottom (180 px) and right (320 px)
  overscan values have no effect.

What does fix it: the Vulkan **render target implementation path**. In the
default host-render-target path (`fbo`) the save screens are clipped to x>=320;
with the fragment-shader-interlock path (`fsi`) they render correctly. The path
is selected by the `render_target_path_vulkan` cvar (init-only); it is now set to
`"fsi"` in `out/build/linux-amd64-release/beautiful_katamari.toml`. The runtime
falls back to `fbo` automatically if the device does not expose
`VK_EXT_fragment_shader_interlock` (`render_target_cache.cpp`,
`fsi_path_supported`). `fsi` is the accuracy-oriented EDRAM path (pixel shader
interlock, per-pixel EDRAM addressing), so a small performance cost is expected;
on the GTX 1050 Ti here the game still runs at 720p without visible stutter.

Not fully diagnosed: *why* the fbo path clips at exactly width/4 on these
screens. The guest probably renders them through a window offset / scissor pair
(`PA_SC_WINDOW_OFFSET` applied to the scissor but not to the viewport, or an
EDRAM-aligned render-target origin), which the host-render-target path gets
wrong. Confirming that needs draw-level tracing inside `librexgpu-xenos.so`,
which is prebuilt and has no debug info; `--log_level=trace` gives per-draw
`REXGPU_TRACE` lines but nothing that dumps the scissor/window-offset state.

#### 9.5.2 The results screen does not show the katamari (fixed)

After every level the King's results cutscene was missing the katamari that he
holds up in his raised hand and then throws into space — the hand was visibly
empty. The Xenia issue tracker has the same report (xenia-canary
game-compatibility #202: "the katamari not appearing during the results screen,
making it look like the King throws nothing into space before the star appears";
older Xenia builds drew a grey rectangle there, xenia-project game-compatibility
#297), together with the note that photos taken with the in-game camera never
appear in the Photo Album.

That pairing is the clue: both are **rendered images**, not models. The guest
renders the katamari to a render target, resolves it, and then reads the pixels
back on the CPU to store the "photo"; the results cutscene displays that stored
image as a flat cut-out (visible in the screenshot, the katamari above the King's
hand is a photo with a transparent background, not a 3D model). The album photos
use the same mechanism.

The Vulkan backend does not sync host render targets back to guest-visible
memory by default (`readback_resolve = "none"`), because a sync readback stalls
the GPU. A guest CPU read of memory that still holds an unresolved host render
target therefore sees stale/zero data, so the "photo" is blank — transparent in
this runtime, grey in the Xenia builds that allocated the target differently.

The fix is one cvar in `out/build/linux-amd64-release/beautiful_katamari.toml`:

```toml
readback_resolve = "full"
```

`full` performs an immediate, synchronised readback of the resolved range
(`IssueCopy_ReadbackResolvePath` in `src/graphics/vulkan/command_processor.cpp`).
Verified by playing a level through to the results screen: the katamari is now
in the King's hand and is thrown into space (see
`2026-11-01_results_katamari_fixed.png`). The album side of the bug follows from
the same path but has not been re-checked with an actual camera photo.

Cost: every resolve now syncs back to guest memory, which is the accurate but
stalling mode. If heavier stages drop frames, `"fast"` (read the previous frame,
copy every frame) or `"some"` (read the previous frame on cache miss) trade some
freshness for speed. Note this is independent of the `fsi`/`fbo` choice in §9.5.1
— the fixed screenshot was taken on the `fsi` path with readback enabled.

### 9.6 Performance in the hub world and levels (open)

Symptom: the hub world and parts of the levels feel sluggish (worse than real
hardware), and while the game runs the whole desktop becomes unresponsive
(the GTX 1050 Ti is pinned, so the compositor is starved).

Reliable measurement: launch with `--log_level=debug` and count
`XELOG_GPU PRESENT:` lines per second. One line = one presented guest frame, so
this is an exact fps counter and it is immune to the window being occluded or
minimised (screen capture is not: x11grab reports 0 fps for a covered window).

Measured on 2026-11-01 (Ryzen 5 3600, GTX 1050 Ti, driver 580.178.04, guest
output always 1280x720, `fsi`, `readback_resolve = "full"`):

| scene | presented fps |
|-------|---------------|
| logo / title / menus | 45-55 |
| loading transitions | 1-4 |
| hub (garden with the Prince) | 18.5 (`fsi`) / 20.6 (`fbo`) |

GPU during the hub: 98 % utilisation, ~90-96 % SM in `nvidia-smi pmon`,
50-55 % memory-controller utilisation, 1746 MHz (near the 1974 MHz max), 61 °C,
no power/thermal cap. The guest process uses ~290 % of 1200 % CPU. So this is
**GPU-bound by ~50 ms/frame**, not a CPU or synchronisation stall; a stall would
show up as the GPU going idle, which it does not.

Ruled out:

- **Render target path.** `fsi` and `fbo` are within 10 % of each other in the
  same hub scene (18.5 vs 20.6 fps), so the accuracy path is not the cost. Keep
  `fsi`; the band bug below needs it anyway.
- **Lowering the guest resolution.** `--video_mode_width/height` (and the
  `video_mode_*` cvars, confirmed applied via the startup dump) does *not*
  change the guest render resolution: the present log still reports
  `packet_size=1280x720 ... guest_output_size=1280x720`. The game sizes its own
  frontbuffer, so there is no cheap "render at 960x540 and upscale" knob. The
  only sub-1x knob would have to be added to the runtime.
- **`readback_resolve = "full"`** is *not* a per-frame cost here: the GPU is
  98 % busy, not blocked, so the synchronous readback is not stalling the
  pipeline in the hub. It is left at `full` so §9.5.2 (results photo) keeps
  working; switching to `none` was *not* shown to buy anything.
- **fbo band workaround.** `execute_unclipped_draw_vs_on_cpu_for_psi_render_backend
  = false` does not remove the band.

New finding about `fbo` (worth recording because it explains the band class
better): it is not a save-screen-only clip but a **quarter-frame** bug. In the
save "Select data to load" screen x<320 is black (and UI is cut at x=320), while
in the hub with the very same path x>=960 of a 1280-wide frame is black
(measured pixel-exactly: rightmost non-black column = 959). Both times exactly
one quarter of the frame is missing, on opposite sides. `fsi` renders both
correctly, so `fsi` stays.

Diagnostics added: `BeautifulKatamariApp::OnPostSetup()` now prints
`[bk] effective graphics settings:` to stderr at startup (every value is the
effective post-config value, read through `rex::cvar::Query`). Recorded
defaults: `vsync=true`, `resolution_scale=1`, `draw_resolution_scale_x/y=1`,
`native_2x_msaa=true`, `direct_host_resolve=true`,
`gamma_render_target_as_unorm16=true`, `snorm16_render_target_full_range=true`,
`readback_memexport=true`, `readback_memexport_fast=true`,
`occlusion_query_enable=true`, `query_occlusion_fake_sample_count=1000`,
`vulkan_dynamic_rendering=true`, `vulkan_sparse_shared_memory=true`,
`vulkan_allow_present_mode_{immediate,mailbox,fifo_relaxed}=true`,
`present_effect=bilinear`, `present_dither=false`, `present_safe_area_x/y=90`.
The session creates only six render targets in total (no per-frame render
target churn).

Next experiments, in order of expected value:

1. `occlusion_query_enable = false` (falls back to the constant
   `query_occlusion_fake_sample_count`). Host occlusion queries need a query
   pool plus result readback, and a culling-heavy scene like the hub is exactly
   where a title issues many of them. Cheap to test, no visual risk beyond
   slightly different visibility culling.
2. `readback_memexport = false` (and/or `readback_memexport_fast`): memexport
   readback is a GPU/CPU sync path that is on by default.
3. Confirm the 4x MSAA usage. The only render target views logged are 4xMSAA
   (640x1024, 320x2048) and 1x (1280x2048), i.e. the title does use 4x MSAA
   somewhere. `native_2x_msaa` only accelerates 2x; a 4x MSAA 720p scene is
   ~4x the fill of 1x, which matches a 50 ms frame.
