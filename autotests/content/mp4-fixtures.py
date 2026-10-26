#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Spherical Video V2, st3d semantics 0..4; ISO BMFF sized box traversal.
import hashlib
import json
from pathlib import Path
import struct
import sys
root = Path(sys.argv[1])
rows = json.loads((root/'manifest.json').read_text())
source = (root/'sphere-360-1.mp4').read_bytes()
containers = {b'moov':0, b'trak':0, b'mdia':0, b'minf':0, b'stbl':0, b'stsd':8, b'avc1':78}
def rewrite(data, mode, keep_vexu=False, reverse=False, duplicate=False):
    boxes=[]
    offset=0
    while offset < len(data):
        size, tag = struct.unpack_from('>I4s', data, offset)
        assert size >= 8 and offset+size <= len(data)
        payload=data[offset+8:offset+size]
        offset+=size
        if tag in (b'sv3d',) or (tag==b'vexu' and not keep_vexu):
            continue
        if tag==b'st3d':
            assert len(payload)==5
            payload=payload[:4]+bytes([mode])
        if tag in containers:
            skip=containers[tag]
            payload=payload[:skip]+rewrite(payload[skip:], mode, keep_vexu, reverse, duplicate)
        boxes.append((tag,struct.pack('>I4s',8+len(payload),tag)+payload))
        if duplicate and tag==b'st3d':
            boxes.append(boxes[-1])
    if reverse and any(tag==b'st3d' for tag,_ in boxes) and any(tag==b'vexu' for tag,_ in boxes):
        # Place vexu before st3d without changing the sample entry's byte count.
        a=next(i for i,(tag,_) in enumerate(boxes) if tag==b'st3d')
        b=next(i for i,(tag,_) in enumerate(boxes) if tag==b'vexu')
        boxes[a],boxes[b]=boxes[b],boxes[a]
    return b''.join(box for _,box in boxes)
for mode,layout,order in [(0,0,0),(1,2,0),(2,1,0),(3,-1,0),(4,1,1)]:
    file=root/f'st3d-{mode}.mp4'
    file.write_bytes(rewrite(source,mode))
    rows.append(dict(file=file.name,base='video/mp4',stereo=int(mode!=0),layout=layout,order=order,projection=-1))
for reverse in (False,True):
    file=root/f'st3d-vexu-{int(reverse)}.mp4'
    file.write_bytes(rewrite(source,2,True,reverse))
    rows.append(dict(file=file.name,base='video/mp4',stereo=1,layout=1,order=0))
for name,mode,duplicate in [('st3d-reserved.mp4',255,False),('st3d-duplicate.mp4',2,True)]:
    file=root/name
    file.write_bytes(rewrite(source,mode,duplicate=duplicate))
    rows.append(dict(file=name,base='video/mp4',stereo=-1,layout=0,order=0,projection=-1,
                     status=3 if duplicate else 0))
for row in rows:
    row['sha256']=hashlib.sha256((root/row['file']).read_bytes()).hexdigest()
(root/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n')
