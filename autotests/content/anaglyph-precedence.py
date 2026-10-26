#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Codec packing replaces a container's anaglyph, never mixes their fields."""
import json
import subprocess
import sys
from pathlib import Path
references, output = map(Path, sys.argv[1:3]); probe = sys.argv[3]
output.mkdir(parents=True,exist_ok=True)
rows=[]
for sei,layout in [(3,1),(4,2)]:
    for mark in [10,12]:
        path=output/f'codec-{sei}-container-{mark}.mkv'
        if not path.exists():
            subprocess.run(['ffmpeg','-nostdin','-v','error','-i',str(references/f'avc-sei-{sei}.mp4'),
                            '-c','copy','-metadata:s:v',f'stereo_mode={mark}',str(path)],check=True)
        got=json.loads(subprocess.check_output([probe,'--base','video/x-matroska','--json',str(path)]))
        expected={'stereo':1,'layout':layout,'order':0,'anaglyph':0}
        rows.append({'file':path.name,'expected':expected,'actual':got,
                     'pass':all(got[k]==v for k,v in expected.items())})
print(json.dumps(rows,indent=2))
raise SystemExit(any(not r['pass'] for r in rows))
