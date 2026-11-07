# Beautiful Katamari Recompilation (BKReXGlue)

A PC port of **Beautiful Katamari** (Xbox 360) that recompiles the game's
original PowerPC code to native x86-64, running through a Vulkan renderer
instead of the original Xbox 360 graphics hardware.

Think of it as a fan-made PC version — the game logic has been translated
from Xbox 360 code to PC code, and we're building a modern graphics renderer
to display it.

---

## What works

- The game boots and reaches the main menu
- Audio plays (music and sound effects)
- Controller input works
- Vulkan graphics pipeline initializes successfully
- The game runs at a playable frame rate in most scenes

## What's in progress

- **Native Vulkan rendering**: The game currently renders through an
  emulated Xbox 360 GPU layer. We're building a native Vulkan renderer that
  will eventually replace it for better performance and compatibility.
  When enabled, you may see a black screen — this is expected while it's
  under development. You can disable it to use the emulated path instead.
- **Full game data**: The game data folder may be missing some assets.
  Extracting from the original game ISO can help.

---

## How to run

### From the build directory

```bash
cd /mnt/sdb/bkrecomp/BKReXGlue/out/build/linux-amd64-release

./beautiful_katamari \
  --game_data_root="/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)"
```

### Using the launcher script

```bash
cd /mnt/sdb/bkrecomp/BKReXGlue
./run.sh
```

### Common options

| Option | What it does |
|--------|--------------|
| `--no-native` | Use the emulated Xbox 360 GPU (recommended for now) |
| `--native` | Try the experimental native Vulkan renderer |
| `--fps-cap=60` | Limit the game to 60 frames per second |
| `--debug=2` | Show debug visualization (solid colors per object) |

Example:
```bash
./run.sh --no-native --fps-cap=60
```

---

## Configuration

Edit `config.toml` in the build directory to change default settings:

```toml
# Use emulated GPU (recommended - works reliably)
bk_native_render_scene = false

# Cap frame rate (0 = unlimited)
bk_guest_fps_cap = 0.0

# Vertical sync (on = smoother, off = faster but may tear)
vsync = false
```

You can also press **F1** in-game to open a configuration dialog.

---

## Troubleshooting

### Black screen

If the screen is black, the native Vulkan renderer is likely enabled but
not yet producing output. Fix it:

```
./run.sh --no-native
```

Or set `bk_native_render_scene = false` in `config.toml`.

### Low frame rate (30–40 FPS in levels)

The frame rate depends on your hardware and how far along the native
renderer is. Try:

- Disabling vsync: `vsync = false` in `config.toml`
- Removing the FPS cap: `bk_guest_fps_cap = 0.0` in `config.toml`

### Game won't start

Make sure the game data path is correct. The folder should contain files
like `d02.dat`, `d03.dat`, `d04.dat`, `d05.dat`, and `default.xex`.

---

## Project layout

```
/mnt/sdb/bkrecomp/BKReXGlue/
├── config.toml              # runtime settings
├── run.sh                   # launch script
├── out/build/.../           # compiled game binary
│   └── beautiful_katamari   # the executable
└── src/native/              # native renderer source code
```

---

## About this project

This is a work in progress. The game has been recompiled from Xbox 360
PowerPC code to PC x86-64 code using the rexglue-sdk toolkit. The graphics
renderer is being rebuilt from scratch using Vulkan.

If you're interested in helping or following development, check the
`tools/native_discovery/` folder for scripts that help analyze the game's
render functions.

---

**Note**: You need a legal copy of Beautiful Katamari to use this project.
The game data files are not included — point the launcher at your own
game installation.