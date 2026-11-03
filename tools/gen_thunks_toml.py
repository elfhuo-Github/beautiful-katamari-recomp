#!/usr/bin/env python3
"""Emit bk_gapfill_thunks.toml from the bctr-preceded swallowed-thunk candidates."""
import json

cands = json.load(open("/tmp/opencode/thunk_candidates.json"))
sel = [c for c in cands if c["prev"] == "bctr" and c["size"] >= 4]

lines = []
lines.append("# Beautiful Katamari - GapFill-swallowed leaf thunks (auto-generated).")
lines.append("#")
lines.append("# GapFill (src/codegen/phase_gapfill.cpp) splits uncovered code regions")
lines.append("# only on `blr` and on direct branches to already-known callables. A leaf")
lines.append("# thunk that ends in an *indirect* terminator (`bctr`) is therefore not a")
lines.append("# split point, so a run of consecutive thunks is registered as a single")
lines.append("# gap-fill function spanning all of them. Only the first thunk gets code;")
lines.append("# the later ones have no entry, so an indirect call to one hits the runtime")
lines.append("# \"Call to invalid or unregistered function\" fatal.")
lines.append("#")
lines.append("# The addresses below are exactly the block starts the previous XenonRecomp")
lines.append("# build emitted *immediately after a bctr* that rexglue did not register on")
lines.append("# its own. Sizes were measured against the previous build's block layout")
lines.append("# (next boundary - address). Regenerate with tools/find_thunks2.py.")
lines.append("#")
lines.append("# NOTE: this list only grows when the runtime surfaces another address; it")
lines.append("# is a workaround for the GapFill blind spot, not a complete recovery.")
lines.append("")
lines.append("[functions]")
for c in sel:
    lines.append(f'"0x{c["addr"]:08X}" = {{ size = 0x{c["size"]:X} }}')
lines.append("")

open("/mnt/sdb/bkrecomp/BKReXGlue/bk_gapfill_thunks.toml", "w").write("\n".join(lines))
print(f"wrote {len(sel)} entries")
for c in sel[:10]:
    print(f"  0x{c['addr']:08X} size=0x{c['size']:X} first='{c['first']}'")
