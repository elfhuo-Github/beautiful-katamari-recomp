#!/usr/bin/env python3
"""
Beautiful Katamari - native renderer function discovery (v2).

Scans the recompiled guest code (BKRecomp/ppc/ppc_recomp.*.cpp) for functions
that are candidates for native renderer hooks. Instead of looking for D3D
names (which don't exist in PPC recompiled code), it looks for:

  1. Functions that call PPC_CALL_INDIRECT_FUNC (virtual dispatch / function
     table calls - typical of render engine virtual calls)
  2. Functions that contain buffer/register patterns typical of GPU command
     submission
  3. Functions with high call frequency (render loop candidates)
  4. Functions that reference Xenos register ranges or MMIO

Usage:
  python3 find_render_funcs2.py <path/to/BKRecomp/ppc> [output.toml]
"""

import re
import sys
import os
from collections import defaultdict

# Patterns that suggest render-related operations in recompiled PPC code.
# These look for indirect calls (virtual dispatch), buffer bindings, etc.
INDIRECT_CALL = re.compile(r"PPC_CALL_INDIRECT_FUNC")
MMIO_ACCESS = re.compile(r"0x7[EF][0-9A-F]{6}|0x8000[0-9A-F]{4}")
BUFFER_BIND = re.compile(r"SetStreamSource|SetIndices|SetShader|SetConstant|DrawIndexed|DrawVertices|BeginVertices", re.IGNORECASE)
REGISTER_WRITE = re.compile(r"ctx\.r\d+\.u64\s*=\s*ctx\.r\d+\.u64")

# Track function -> patterns found
func_patterns = defaultdict(list)
func_sizes = {}
all_functions = []


def scan_file(path):
    """Scan a single ppc_recomp file for render-related functions."""
    with open(path, "r", errors="replace") as f:
        content = f.read()

    # Find function implementations
    func_pattern = re.compile(
        r"PPC_FUNC_IMPL\(__imp__?(sub_[0-9A-Fa-f]+)\)\s*\{(.*?)\n\}",
        re.DOTALL,
    )

    for m in func_pattern.finditer(content):
        func_name = m.group(1)
        func_body = m.group(2)
        all_functions.append(func_name)

        # Check for indirect calls (virtual dispatch)
        if INDIRECT_CALL.search(func_body):
            func_patterns[func_name].append("indirect_call")

        # Check for MMIO access (GPU registers)
        if MMIO_ACCESS.search(func_body):
            func_patterns[func_name].append("mmio")

        # Check for buffer bind patterns
        if BUFFER_BIND.search(func_body):
            func_patterns[func_name].append("buffer_bind")

        # Check for register write patterns (state setup)
        reg_writes = len(REGISTER_WRITE.findall(func_body))
        if reg_writes > 5:
            func_patterns[func_name].append(f"reg_writes_{reg_writes}")

        # Estimate size (lines of code)
        func_sizes[func_name] = len(func_body.splitlines())


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    ppc_dir = sys.argv[1]
    output = sys.argv[2] if len(sys.argv) > 2 else None

    for fname in sorted(os.listdir(ppc_dir)):
        if not fname.startswith("ppc_recomp.") or not fname.endswith(".cpp"):
            continue
        path = os.path.join(ppc_dir, fname)
        scan_file(path)

    # Score functions by how many render-like patterns they match
    scored = []
    for func in all_functions:
        patterns = func_patterns.get(func, [])
        score = len(patterns)
        if score > 0:
            scored.append((score, func, patterns, func_sizes.get(func, 0)))

    scored.sort(reverse=True)

    print("=== BK Native Renderer Function Discovery (v2) ===")
    print(f"Scanned directory: {ppc_dir}")
    print(f"Total functions found: {len(all_functions)}")
    print(f"Functions with render-like patterns: {len(scored)}")
    print()
    print("--- Top candidates (by pattern count) ---")
    print(f"{'Score':<6} {'Size':<6} {'Function':<16} {'Patterns'}")
    print("-" * 80)
    for score, func, patterns, size in scored[:50]:
        print(f"{score:<6} {size:<6} {func:<16} {','.join(patterns)}")

    if output:
        with open(output, "w") as f:
            f.write("# Discovered render function candidates for BK native renderer\n")
            f.write("# Review each address before adding to bk_config.toml or hooks\n")
            f.write("\n[render_functions]\n")
            for score, func, patterns, size in scored:
                addr = func.replace("sub_", "0x")
                f.write(f"# {func} -> {addr}  score={score} size={size} patterns={','.join(patterns)}\n")
        print(f"\nWritten to {output}")


if __name__ == "__main__":
    main()
