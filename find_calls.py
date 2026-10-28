# -*- coding: utf-8 -*-
import re, pathlib
p = pathlib.Path('src/App.cpp')
s = p.read_text(encoding='utf-8')
for ln_no,line in enumerate(s.splitlines(),1):
    if 'fetchTEC' in line or 'glo.' in line.lower():
        print(f'{ln_no}: {line.strip()[:120]}')