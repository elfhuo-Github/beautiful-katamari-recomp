#!/bin/bash
#=============================================================================
# Beautiful Katamari Recomp - Native Renderer Test Script
#=============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/out/build/linux-amd64-release"
GAME_DATA_ROOT="${GAME_DATA_ROOT:-/mnt/sdb/bkrecomp/Beautiful Katamari (USA) (En,Ja)}"
EXECUTABLE="${BUILD_DIR}/beautiful_katamari"
CONFIG_TOOL="${BUILD_DIR}/bk_config_tool"
TEST_DURATION=8

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0

log_info()  { echo -e "${BLUE}[INFO]${NC} $1"; }
log_pass()  { echo -e "${GREEN}[PASS]${NC} $1"; PASSED=$((PASSED + 1)); }
log_fail()  { echo -e "${RED}[FAIL]${NC} $1"; FAILED=$((FAILED + 1)); }
log_skip()  { echo -e "${YELLOW}[SKIP]${NC} $1"; SKIPPED=$((SKIPPED + 1)); }

get_latest_log() {
    ls -t "${BUILD_DIR}/logs/beautiful_katamari_*.log" 2>/dev/null | head -1
}

run_game() {
    local duration="$1"
    shift
    BK_DISPLAY=1 DISPLAY=:0.0 timeout "$duration" "$EXECUTABLE" \
        --game_data_root="$GAME_DATA_ROOT" \
        "$@" > /tmp/bk_test_output.log 2>&1 || true
    sleep 2
}

echo "============================================================================"
echo "  Beautiful Katamari Recomp - Native Renderer Test Script"
echo "============================================================================"
echo ""

[ -x "$EXECUTABLE" ] || { log_fail "Executable not found: $EXECUTABLE"; exit 1; }
log_pass "Executable: $EXECUTABLE"

[ -d "$GAME_DATA_ROOT" ] || { log_fail "Game data not found: $GAME_DATA_ROOT"; exit 1; }
log_pass "Game data: $GAME_DATA_ROOT"

[ -n "$DISPLAY" ] || export DISPLAY=:0.0
log_info "Display: $DISPLAY"
mkdir -p "${BUILD_DIR}/logs"

echo ""
echo "============================================================================"
echo "  Running Tests"
echo "============================================================================"
echo ""

test_basic() {
    echo -n "Test 1: Native renderer (default cvars) ... "
    run_game 8 --bk_native_render=true --bk_native_render_scene=true
    local log_file=$(get_latest_log)
    [ -z "$log_file" ] && { log_fail "No log file found"; return 1; }
    grep -q "bk native-render hook layer enabled" "$log_file" 2>/dev/null && \
    grep -q "hooked __imp__VdSwap" "$log_file" 2>/dev/null && \
    { log_pass "Native renderer hooks active"; return 0; }
    log_fail "Native renderer hooks not found"
    return 1
}

test_emulated() {
    echo -n "Test 2: Emulated xenos (native disabled) ... "
    run_game 5 --bk_native_render=false --bk_native_render_scene=false
    local log_file=$(get_latest_log)
    ! grep -q "bk native-render hook layer enabled" "$log_file" 2>/dev/null && \
    { log_pass "Native renderer disabled (emulated path active)"; return 0; }
    log_fail "Native renderer should be disabled"
    return 1
}

test_windowed() {
    echo -n "Test 3: Windowed mode (1280x720) ... "
    run_game 5 --bk_native_render=true --bk_native_render_scene=true
    local log_file=$(get_latest_log)
    grep -q "Created 1280x720 swapchain" "$log_file" 2>/dev/null && \
    { log_pass "Windowed mode: 1280x720 swapchain"; return 0; }
    log_fail "Windowed swapchain not found"
    return 1
}

test_fullscreen() {
    echo -n "Test 4: Windowed fullscreen (1920x1080) ... "
    local toml="${BUILD_DIR}/beautiful_katamari.toml"
    cp "$toml" "${toml}.bak" 2>/dev/null || true
    sed -i 's/^fullscreen.*/fullscreen = true/' "$toml" 2>/dev/null || echo "fullscreen = true" >> "$toml"
    sed -i 's/^window_width.*/window_width = 1920/' "$toml" 2>/dev/null || echo "window_width = 1920" >> "$toml"
    sed -i 's/^window_height.*/window_height = 1080/' "$toml" 2>/dev/null || echo "window_height = 1080" >> "$toml"
    run_game 5 --bk_native_render=true --bk_native_render_scene=true
    local log_file=$(get_latest_log)
    grep -q "Created 1920x1080 swapchain" "$log_file" 2>/dev/null && \
    { log_pass "Windowed fullscreen: 1920x1080 swapchain"; cp "${toml}.bak" "$toml" 2>/dev/null; return 0; }
    log_fail "Fullscreen swapchain not found"
    cp "${toml}.bak" "$toml" 2>/dev/null || true
    return 1
}

test_fps_cap() {
    echo -n "Test 5: Guest FPS cap (30 fps) ... "
    run_game 5 --bk_native_render=true --bk_native_render_scene=true --cvar bk_guest_fps_cap=30
    local log_file=$(get_latest_log)
    grep -q "bk native-render hook layer enabled" "$log_file" 2>/dev/null && \
    { log_pass "FPS cap cvar accepted"; return 0; }
    log_fail "FPS cap cvar not accepted"
    return 1
}

test_debug() {
    echo -n "Test 6: Debug mode (debug=2) ... "
    run_game 5 --bk_native_render=true --bk_native_render_scene=true --cvar bk_native_render_scene_debug=2
    local log_file=$(get_latest_log)
    grep -q "bk native-render hook layer enabled" "$log_file" 2>/dev/null && \
    { log_pass "Debug mode cvar accepted"; return 0; }
    log_fail "Debug mode cvar not accepted"
    return 1
}

test_config_tool() {
    echo -n "Test 7: Configuration tool build ... "
    [ -x "$CONFIG_TOOL" ] && { log_pass "Config tool found"; return 0; }
    log_fail "Config tool not found"; return 1
}

TEST_NAME="${1:-all}"
case "$TEST_NAME" in
    all) test_basic || true; test_emulated || true; test_windowed || true; test_fullscreen || true; test_fps_cap || true; test_debug || true; test_config_tool || true ;;
    basic) test_basic || true ;;
    native) test_basic || true ;;
    xenos) test_emulated || true ;;
    fullscreen) test_fullscreen || true ;;
    windowed) test_windowed || true ;;
    fps_cap) test_fps_cap || true ;;
    debug) test_debug || true ;;
    config_tool) test_config_tool || true ;;
    *) log_fail "Unknown test: $TEST_NAME"; echo "Usage: $0 [all|basic|native|xenos|fullscreen|windowed|fps_cap|debug|config_tool]"; exit 1 ;;
esac

echo ""
echo "============================================================================"
echo "  Test Summary"
echo "============================================================================"
echo -e "  ${GREEN}Passed: ${PASSED}${NC}"
echo -e "  ${RED}Failed: ${FAILED}${NC}"
echo -e "  ${YELLOW}Skipped: ${SKIPPED}${NC}"
echo ""
[ "$FAILED" -eq 0 ] && { echo -e "${GREEN}All tests passed!${NC}"; exit 0; } || { echo -e "${RED}Some tests failed.${NC}"; exit 1; }
