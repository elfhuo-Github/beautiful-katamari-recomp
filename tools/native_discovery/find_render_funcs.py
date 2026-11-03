#!/usr/bin/env python3
"""
Beautiful Katamari - native renderer function discovery.

Scans the recompiled guest code (BKRecomp/ppc/ppc_recomp.*.cpp) for functions
that are candidates for native renderer hooks:

  1. Draw functions (DrawIndexedVertices, DrawVertices, BeginVertices) -
     the guest's D3D-equivalent draw calls.
  2. RenderMesh / mesh submission functions - per-visible-mesh entry points.
  3. Scene draw list functions - sorted world geometry dispatch.
  4. Swap/present function - the frame boundary hook.
  5. Constant bank setup (SetPending_AluConstants equivalent) - VS/PS
     constant upload tracking.
  6. Camera setup functions - view/projection matrix setup.

Usage:
  python3 find_render_funcs.py <path/to/BKRecomp/ppc> [output.toml]

The output is a TOML snippet suitable for inclusion in bk_config.toml with
the discovered addresses.
"""

import re
import sys
import os

# Patterns that indicate a render-related function.
DRAW_PATTERNS = [
    re.compile(r"DrawIndexedVertices", re.IGNORECASE),
    re.compile(r"DrawVertices\b", re.IGNORECASE),
    re.compile(r"BeginVertices", re.IGNORECASE),
    re.compile(r"DrawIndexedPrimitive", re.IGNORECASE),
    re.compile(r"DrawPrimitive", re.IGNORECASE),
]

RENDERMESH_PATTERNS = [
    re.compile(r"RenderMesh", re.IGNORECASE),
    re.compile(r"SubmitMesh", re.IGNORECASE),
    re.compile(r"DrawMesh", re.IGNORECASE),
]

SCENE_PATTERNS = [
    re.compile(r"SceneRenderView", re.IGNORECASE),
    re.compile(r"RenderView", re.IGNORECASE),
    re.compile(r"SortList", re.IGNORECASE),
]

SWAP_PATTERNS = [
    re.compile(r"XE_SWAP", re.IGNORECASE),
    re.compile(r"Present", re.IGNORECASE),
    re.compile(r"Swap", re.IGNORECASE),
]

CONSTANT_PATTERNS = [
    re.compile(r"SetPending.*Constant", re.IGNORECASE),
    re.compile(r"SetConstant", re.IGNORECASE),
    re.compile(r"ConstantBank", re.IGNORECASE),
]


def scan_file(path):
    """Scan a single ppc_recomp file for render-related functions."""
    results = {
        "draw": [],
        "render_mesh": [],
        "scene": [],
        "swap": [],
        "constant": [],
    }

    with open(path, "r", errors="replace") as f:
        content = f.read()

    # Find function definitions (sub_XXXXXXXX)
    # The recompiled code has functions labeled like:
    # void sub_822FE988(PPCContext& ctx, uint8_t* base) { ... }
    func_pattern = re.compile(
        r"(?:void|static\s+void)\s+(sub_[0-9A-Fa-f]+)\s*\(PPCContext",
    )

    current_func = None
    func_start = None
    brace_depth = 0

    for line_num, line in enumerate(content.splitlines(), 1):
        m = func_pattern.search(line)
        if m:
            # Save previous function if any
            if current_func:
                _classify(results, current_func, func_start_content)
            current_func = m.group(1)
            func_start = line_num
            func_start_content = line
            brace_depth = line.count("{") - line.count("}")
            continue

        if current_func is not None:
            func_start_content += "\n" + line
            brace_depth += line.count("{") - line.count("}")
            if brace_depth <= 0:
                # Function ended
                _classify(results, current_func, func_start_content)
                current_func = None
                func_start_content = ""

    # Last function
    if current_func:
        _classify(results, current_func, func_start_content)

    return results


def _classify(results, func_name, func_body):
    """Classify a function body into render categories."""
    for pat in DRAW_PATTERNS:
        if pat.search(func_body):
            results["draw"].append(func_name)
            break
    for pat in RENDERMESH_PATTERNS:
        if pat.search(func_body):
            results["render_mesh"].append(func_name)
            break
    for pat in SCENE_PATTERNS:
        if pat.search(func_body):
            results["scene"].append(func_name)
            break
    for pat in SWAP_PATTERNS:
        if pat.search(func_body):
            results["swap"].append(func_name)
            break
    for pat in CONSTANT_PATTERNS:
        if pat.search(func_body):
            results["constant"].append(func_name)
            break


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    ppc_dir = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else None

    all_results = {
        "draw": [],
        "render_mesh": [],
        "scene": [],
        "swap": [],
        "constant": [],
    }

    for fname in sorted(os.listdir(ppc_dir)):
        if not fname.startswith("ppc_recomp.") or not fname.endswith(".cpp"):
            continue
        path = os.path.join(ppc_dir, fname)
        res = scan_file(path)
        for k in all_results:
            all_results[k].extend(res[k])

    # Deduplicate
    for k in all_results:
        all_results[k] = sorted(set(all_results[k]))

    # Print summary
    print("=== BK Native Renderer Function Discovery ===")
    print(f"Scanned directory: {ppc_dir}")
    print()
    for category, funcs in all_results.items():
        print(f"--- {category.upper()} ({len(funcs)}) ---")
        for f in funcs:
            print(f"  {f}")
        print()

    if output:
        import tomllib  # Python 3.11+
        # Write TOML snippet
        with open(output, "w") as f:
            f.write("# Discovered render function candidates for BK native renderer\n")
            f.write("# Review each address before adding to bk_config.toml\n")
            f.write("\n[render_functions]\n")
            for category, funcs in all_results.items():
                f.write(f"\n# {category.upper()}\n")
                for func in funcs:
                    # Extract hex address from sub_XXXXXXXX
                    addr = func.replace("sub_", "0x")
                    f.write(f'# {func} -> {addr}\n')
        print(f"Written to {output}")


if __name__ == "__main__":
    main()
