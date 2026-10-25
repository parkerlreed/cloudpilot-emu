"""Read portable sources or include directories from the pinned upstream Makefiles."""
from pathlib import Path
import re
import sys
root=Path(sys.argv[1])/'src'
backend=sys.argv[2] if len(sys.argv)>2 else 'uarm'
includes=len(sys.argv)>3 and sys.argv[3]=='includes'
for name in ['common',backend]:
    collecting=False
    for line in (root/name/'Makefile').read_text().splitlines():
        if re.match(r'^'+('INCLUDE' if includes else r'SOURCE_C(?:XX)?')+r'\s*=',line):
            collecting=True
            line=line.split('=',1)[1]
        if not collecting:
            continue
        for value in line.replace('\\',' ').split():
            print(root/name/(value[2:] if includes else value))
        collecting=line.rstrip().endswith('\\')
