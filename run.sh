#!/bin/bash
#=============================================================================
# Beautiful Katamari Recomp - Launch Script
#=============================================================================
# Usage: ./run.sh [options]
#
# Options:
#   --native          Enable native renderer (default: enabled)
#   --no-native       Use emulated xenos GPU instead
#   --fps-cap=N       Cap frame rate (0 = uncapped)
#   --debug=N         Debug visualization mode: 0-4
#   --fullscreen      Fullscreen mode
#   --windowed        Windowed mode
#   --game-data-root=PATH   Path to game data (overrides config.toml)
#   --help            Show this help
#=============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/out/build/linux-amd64-release"
EXECUTABLE="${BUILD_DIR}/beautiful_katamari"

# Read config.toml for default game_data_root
CONFIG_FILE="${BUILD_DIR}/config.toml"
GAME_DATA_ROOT=""

if [ -f "$CONFIG_FILE" ]; then
    GAME_DATA_ROOT=$(grep -oP 'game_data_root\s*=\s*"[^"]*"' "$CONFIG_FILE" | head -1 | sed 's/game_data_root = //' | tr -d '"')
fi

NATIVE_RENDER=true
NATIVE_RENDER_SCENE=true
FPS_CAP=0
DEBUG_MODE=0
FULLSCREEN=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --native)
            NATIVE_RENDER=true
            NATIVE_RENDER_SCENE=true
            shift
            ;;
        --no-native)
            NATIVE_RENDER=false
            NATIVE_RENDER_SCENE=false
            shift
            ;;
        --fps-cap)
            FPS_CAP="$2"
            shift 2
            ;;
        --debug)
            DEBUG_MODE="$2"
            shift 2
            ;;
        --fullscreen)
            FULLSCREEN=true
            shift
            ;;
        --windowed)
            FULLSCREEN=false
            shift
            ;;
        --game-data-root)
            GAME_DATA_ROOT="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --native          Enable native renderer hook layer (default)"
            echo "  --no-native       Use emulated xenos GPU instead"
            echo "  --fps-cap=N       Cap frame rate (0 = uncapped)"
            echo "  --debug=N         Debug visualization mode: 0-4"
            echo "  --fullscreen      Fullscreen mode"
            echo "  --windowed        Windowed mode"
            echo "  --game-data-root=PATH   Path to game data (overrides config.toml)"
            echo "  --help            Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build cvars
CVAR_ARGS="--cvar bk_native_render=$NATIVE_RENDER --cvar bk_native_render_scene=$NATIVE_RENDER_SCENE"
if [ "$FPS_CAP" != "0" ]; then
    CVAR_ARGS="$CVAR_ARGS --cvar bk_guest_fps_cap=$FPS_CAP"
fi
if [ "$DEBUG_MODE" != "0" ]; then
    CVAR_ARGS="$CVAR_ARGS --cvar bk_native_render_scene_debug=$DEBUG_MODE"
fi

echo "============================================================================="
echo "  Beautiful Katamari Recomp - Native Renderer"
echo "============================================================================"
echo ""
echo "  Game data: $GAME_DATA_ROOT"
echo "  Native render: $NATIVE_RENDER"
echo "  Native scene: $NATIVE_RENDER_SCENE"
echo "  FPS cap: $FPS_CAP"
echo "  Debug mode: $DEBUG_MODE"
echo ""

BK_DISPLAY=1 DISPLAY=:0.0 "$EXECUTABLE" \
    --game_data_root="$GAME_DATA_ROOT" \
    $CVAR_ARGS
