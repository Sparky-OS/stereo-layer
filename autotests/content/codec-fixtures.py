#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import re
from pathlib import Path
import subprocess
import sys
from fixtures import run
root = Path(sys.argv[1])
rows = json.loads((root/'manifest.json').read_text())
# H.264 frame-packing SEI types 0..5, authored by the independent encoder.
for mode, layout in enumerate([6, 5, 4, 1, 2, 8]):
    file = root / f'avc-sei-{mode}.mp4'
    run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=64x32:rate=4',
         '-frames:v', '4', '-c:v', 'libx264', '-threads', '1', '-x264-params',
         f'frame-packing={mode}', '-y', str(file)])
    rows.append(dict(file=file.name, base='video/mp4', stereo=1, layout=layout, order=0,
                     signal='avc-frame-packing-sei', value=mode))
# The installed x265 lacks frame-packing. Author H.265 D.2.16 prefix SEI
# explicitly in an Annex B stream, then remux without re-encoding.
raw = root/'hevc-plain.hevc'
run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=64x32:rate=4',
     '-frames:v', '4', '-c:v', 'libx265', '-threads', '1', '-x265-params',
     'pools=none:frame-threads=1:bframes=0', '-y', str(raw)])
nals = [n for n in re.split(b'\x00\x00(?:\x00)?\x01', raw.read_bytes()) if n]
def sei(mode, frame0):
    bits = '1' + '0' + f'{mode:07b}' + '0' + '000001' + '000' + str(frame0) + '00'
    if mode != 5:
        bits += '0' * 16
    bits += '0' * 8 + '1' + '0'  # reserved, persistence, upsampled aspect
    bits += '1'
    bits += '0' * (-len(bits) % 8)
    payload = int(bits, 2).to_bytes(len(bits)//8, 'big')
    rbsp = bytes([45, len(payload)]) + payload + b'\x80'
    escaped = bytearray()
    zeros = 0
    for byte in rbsp:
        if zeros >= 2 and byte <= 3:
            escaped.append(3)
            zeros = 0
        escaped.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return b'\x00\x00\x00\x01\x4e\x01' + escaped
for mode in range(8):
    file = root/f'hevc-sei-{mode}.mp4'
    stream = bytearray()
    frame = 0
    for nal in nals:
        if ((nal[0] >> 1) & 63) <= 31:
            stream += sei(mode, 1 - frame % 2)
            frame += 1
        stream += b'\x00\x00\x00\x01' + nal
    marked = root/f'hevc-sei-{mode}.hevc'
    marked.write_bytes(stream)
    run(['ffmpeg', '-nostdin', '-v', 'error', '-r', '4', '-i', str(marked), '-c', 'copy', '-y', str(file)])
    valid = mode in (3, 4, 5)
    rows.append(dict(file=file.name, base='video/mp4', stereo=1 if valid else -1,
                     layout={3:1, 4:2, 5:8}.get(mode, 0), order=0,
                     signal='hevc-frame-packing-sei', value=mode))
plain = root/'avc-plain.mp4'
run(['ffmpeg', '-nostdin', '-v', 'error', '-i', str(root/'plain.mkv'), '-c:v', 'libx264', '-threads', '1', '-y', str(plain)])
rows.append(dict(file=plain.name, base='video/mp4', stereo=-1, layout=0, order=0))
for coverage in [180, 360, 90, 6]:
    for stereo in [0, 1]:
        for container in ['mp4', 'mkv']:
            file = root / f'sphere-{coverage}-{stereo}.{container}'
            run([sys.argv[2], str(plain), str(file), str(coverage), str(stereo)])
            rows.append(dict(file=file.name, base='video/mp4' if container=='mp4' else 'video/x-matroska',
                             stereo=1 if stereo else -1, layout=stereo, order=0,
                             coverage=360 if coverage == 6 else 0 if coverage == 90 else coverage,
                             projection=1 if coverage==6 else 0 if coverage==360 else 2))
(root/'manifest.json').write_text(json.dumps(rows, indent=2)+'\n')
