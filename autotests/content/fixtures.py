#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Independent fixture authoring: RFC 9559 StereoMode, section 5.1.4.1.28.3.
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

MODES = [
    ('mono', 0, 0), ('left_right', 1, 0), ('bottom_top', 2, 1),
    ('top_bottom', 2, 0), ('checkerboard_rl', 6, 1), ('checkerboard_lr', 6, 0),
    ('row_interleaved_rl', 4, 1), ('row_interleaved_lr', 4, 0),
    ('col_interleaved_rl', 5, 1), ('col_interleaved_lr', 5, 0),
    ('anaglyph_cyan_red', 0, 0), ('right_left', 1, 1),
    ('anaglyph_green_magenta', 0, 0), ('block_lr', 9, 0), ('block_rl', 9, 1),
]

def run(args):
    subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

def generate(root):
    root.mkdir(parents=True, exist_ok=True)
    plain = root / 'plain.mkv'
    run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
         'testsrc2=size=64x32:rate=4', '-frames:v', '2', '-c:v', 'ffv1', '-threads', '1', '-y', str(plain)])
    records = [dict(file=plain.name, base='video/x-matroska', stereo=0, layout=0, order=0)]
    for value, (name, layout, order) in enumerate(MODES):
        target = root / f'matroska-{value:02}.mkv'
        run(['ffmpeg', '-nostdin', '-v', 'error', '-i', str(plain), '-map', '0:v',
             '-c', 'copy', '-metadata:s:v:0', f'stereo_mode={name}', '-y', str(target)])
        records.append(dict(file=target.name, base='video/x-matroska', stereo=int(value != 0),
                            layout=layout, order=order, signal='matroska-stereomode', value=value))
    # A 3D-looking filename is not a mark.
    (root/'holiday-3D-SBS.mkv').write_bytes(plain.read_bytes())
    records.append(dict(file='holiday-3D-SBS.mkv', base='video/x-matroska', stereo=0, layout=0, order=0))
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name+data))
    ihdr = chunk(b'IHDR', struct.pack('>IIBBBBB', 8, 2, 8, 2, 0, 0, 0))
    pixels = chunk(b'IDAT', zlib.compress((b'\0' + bytes(range(24))) * 2))
    for name, extra, stereo, order in [('plain.png', b'', 0, 0),
                                      ('ster-0.png', chunk(b'sTER', b'\0'), 1, 1),
                                      ('ster-1.png', chunk(b'sTER', b'\1'), 1, 0),
                                      ('ster-decoy.png', chunk(b'tEXt', b'comment\0sTER\1'), 0, 0)]:
        (root/name).write_bytes(b'\x89PNG\r\n\x1a\n' + ihdr + extra + pixels + chunk(b'IEND', b''))
        records.append(dict(file=name, base='image/png', stereo=stereo, layout=stereo, order=order))
    for r in records:
        r['sha256'] = hashlib.sha256((root/r['file']).read_bytes()).hexdigest()
    (root/'manifest.json').write_text(json.dumps(records, indent=2)+'\n')

if __name__ == '__main__':
    generate(Path(sys.argv[1]))
