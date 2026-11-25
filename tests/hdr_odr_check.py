#!/usr/bin/env python3
# DW.10 C: 0 simbolos definidos en mas de una cabecera (ODR)
import re, os, sys, glob
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "Utils")
names = ["FieldFormat.h", "StationMerge.h", "GiroHelpers.h", "GridBlend.h", "IdwInterp.h", "IrtamBracket.h"]
pat = re.compile(r'^\s*(?:static|inline)\s+[\w:<>,\s\*&]+\s+(\w+)\s*\(')
seen = {}
for n in names:
    for line in open(os.path.join(root, n), encoding="utf-8", errors="replace"):
        m = pat.match(line)
        if m and not line.strip().startswith("//"):
            seen.setdefault(m.group(1), set()).add(n)
dups = {k: sorted(v) for k, v in seen.items() if len(v) > 1}
print("simbolos:", len(seen), "duplicados:", len(dups))
for k, v in sorted(dups.items()):
    print("  DUP", k, v)
if dups:
    sys.exit(1)
