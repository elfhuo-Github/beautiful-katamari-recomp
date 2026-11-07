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
- Native Vulkan renderer draws game geometry (replaces Xenia xenos GPU emulation)
- The game runs at a playable frame rate in most scenes

## Graphics (native Vulkan only)

Xenia xenos GPU emulation has been removed entirely. The game renders through
the native Vulkan renderer, which hooks the guest's draw calls and renders
them natively. Current limitations:

- Textures: not yet implemented (draws appear as solid colors)
- Camera/view-projection: identity matrix used (no camera extraction yet)
- Vertex format: best-guess from guest memory analysis

To disable native rendering (no GPU path at all):
```
./run.sh --native-scene-disable
```

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

If the screen is black, the native Vulkan renderer may be encountering an
issue. Try:

- Checking that `bk_native_mesh_ctx_vb_bytes` matches your game version
  (try values 0x10 or 0x34 in `config.toml`)
- Using `--debug=2` for solid-color visualization per draw item

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
/mnt/sdb/bkrecomp/
├── Beautiful Katamari (USA) (En,Ja)/   # game data (d02.dat-d05.dat, default.xex)
├── Beautiful Katamari (USA) (En,Ja).iso # original game ISO (7.8 GB)
├── save/                                 # in-game save data (created by game)
├── assets/                               # extracted assets (textures, models, etc.)
└── BKReXGlue/
    ├── config.toml              # runtime settings
    ├── run.sh                   # launch script
    ├── out/build/.../           # compiled game binary
    │   └── beautiful_katamari   # the executable
    └── src/native/              # native renderer source code
```

Save data is stored in `/mnt/sdb/bkrecomp/save/` by default. The path can be
changed in `config.toml` with the `save_path` setting.

Asset files (extracted textures, models, shaders) go in
`/mnt/sdb/bkrecomp/assets/`. The path can be changed with `assets_path` in
`config.toml`.

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