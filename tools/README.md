# tools/

Helper scripts used to build and maintain the codegen configuration. They are
not part of the build; they are kept so the configuration can be regenerated.

Paths are absolute for this checkout (`/mnt/sdb/bkrecomp/...`); adjust if moved.

| script | purpose |
|---|---|
| `find_thunks3.py` | Cross-references the previous XenonRecomp build's per-block disassembly (`BKRecomp/ppc/ppc_recomp.*.cpp`) against the current rexglue function set (`generated/default/codegen.partition.json`) to find function starts rexglue missed: class A (block starts right after a `bctr` inside a GapFill segment) and class B (PDATA ends left uncovered by a skipped GapFill segment). Needs a verbose codegen log (see below) via `$CGLOG`. |
| `gen_thunks_toml.py` | Emits `bk_gapfill_thunks.toml` from the class-A candidates. |
| `whack.py` | Automated recovery loop: build → run → read newest `logs/*.log` → append the fatal address to `bk_gapfill_thunks.toml` as a size-less `[functions]` entry → repeat. |
| `convert_switch_tables.py` | One-off converter for the old XenonAnalyse switch tables (result kept as `bk_switch_tables.toml`, reference only — see MIGRATION.md §4). |

Produce the verbose codegen log that `find_thunks3.py` consumes:

```sh
cd /mnt/sdb/bkrecomp/BKReXGlue
CGLOG=/tmp/opencode/codegen_v.log \
  /path/to/sdk/bin/rexglue codegen beautiful_katamari_manifest.toml --ignore-stamp -v \
  > /tmp/opencode/codegen_v.log 2>&1
CGLOG=/tmp/opencode/codegen_v.log python3 tools/find_thunks3.py
```
