#!/usr/bin/env python3
"""Convert XenonAnalyse [[switch]] tables to rexglue [[switch_tables]].

Old format (BKRecomp/config/BK_switch_tables.toml):
    [[switch]]
    base = 0x8221067C        # bctr address
    r = 11                   # index register number
    default = 0x82210778     # default case (also present in `labels`)
    labels = [ ... ]         # case targets, indexed 0..N-1

New format (rexglue):
    [[switch_tables]]
    address = 0x8221067C     # bctr address
    register = 11            # index register number
    labels = [ ... ]         # case targets, indexed 0..N-1
"""
import re
import sys

SRC = "/mnt/sdb/bkrecomp/BKRecomp/config/BK_switch_tables.toml"
DST = "/mnt/sdb/bkrecomp/BKReXGlue/bk_switch_tables.toml"


def parse(path):
    text = open(path).read()
    # split on [[switch]]
    chunks = re.split(r"^\[\[switch\]\]\s*$", text, flags=re.M)[1:]
    tables = []
    for ch in chunks:
        m_base = re.search(r"^base\s*=\s*(0x[0-9A-Fa-f]+)", ch, re.M)
        m_r = re.search(r"^r\s*=\s*(\d+)", ch, re.M)
        m_def = re.search(r"^default\s*=\s*(0x[0-9A-Fa-f]+)", ch, re.M)
        m_lab = re.search(r"^labels\s*=\s*\[(.*?)\]", ch, re.M | re.S)
        if not (m_base and m_r and m_lab):
            print("SKIP malformed chunk:", ch[:120], file=sys.stderr)
            continue
        labels = re.findall(r"0x[0-9A-Fa-f]+", m_lab.group(1))
        tables.append({
            "base": m_base.group(1),
            "r": m_r.group(1),
            "default": m_def.group(1) if m_def else None,
            "labels": labels,
        })
    return tables


def main():
    tables = parse(SRC)
    missing_default = 0
    out = []
    out.append("# Beautiful Katamari - jump (switch) tables.")
    out.append("#")
    out.append("# Converted from the previous XenonRecomp project's XenonAnalyse output")
    out.append("# (BKRecomp/config/BK_switch_tables.toml). The analyzer used manual bctr")
    out.append("# tables here because these indirect dispatches are not always recovered by")
    out.append("# static heuristics; rexglue applies them as authoritative overrides.")
    out.append("")
    for t in tables:
        if t["default"] is not None and t["default"] not in t["labels"]:
            missing_default += 1
        out.append("[[switch_tables]]")
        out.append(f"address = {t['base']}")
        out.append(f"register = {t['r']}")
        out.append("labels = [" + ", ".join(t["labels"]) + "]")
        out.append("")
    open(DST, "w").write("\n".join(out))
    print(f"Wrote {len(tables)} switch tables to {DST}")
    print(f"Tables whose `default` was not in `labels`: {missing_default}")


if __name__ == "__main__":
    main()
