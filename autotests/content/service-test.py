#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

build, fixtures = map(Path, sys.argv[1:])
def check(file, base, expected, changed):
    result = subprocess.run([str(build/'client-test'), str(file), base, expected, str(int(changed))],
                            capture_output=True, text=True, timeout=3)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()
print('absent:',check(fixtures/'matroska-01.mkv','video/x-matroska','video/x-matroska',False))
service = subprocess.Popen([str(build/'stereo-contentd')], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    for attempt in range(100):
        result = subprocess.run(['gdbus','introspect','--session','--dest','org.stereo.Content1',
                                 '--object-path','/org/stereo/Content1'],capture_output=True)
        if result.returncode == 0:
            break
        time.sleep(.01)
    else:
        raise AssertionError('service did not register')
    print('stereo:',check(fixtures/'matroska-01.mkv','video/x-matroska','video/x-matroska-3d',True))
    print('plain:',check(fixtures/'plain.mkv','video/x-matroska','video/x-matroska',False))
    print('png:',check(fixtures/'ster-0.png','image/png','image/x-png-3d',True))
    with tempfile.TemporaryDirectory(dir=build) as scratch:
        mutable=Path(scratch)/'file.mkv'
        shutil.copyfile(fixtures/'matroska-01.mkv',mutable)
        stat=mutable.stat()
        check(mutable,'video/x-matroska','video/x-matroska-3d',True)
        # Keep inode and restore mtime; ctime must invalidate this cached fact.
        mutable.write_bytes((fixtures/'plain.mkv').read_bytes())
        os.utime(mutable,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        check(mutable,'video/x-matroska','video/x-matroska',False)
        print('rewrite with restored mtime: plain')
    samples=[]
    for n in range(25):
        start=time.monotonic_ns()
        check(fixtures/'matroska-01.mkv','video/x-matroska','video/x-matroska-3d',True)
        samples.append((time.monotonic_ns()-start)/1e6)
    ordered=sorted(samples)
    print(json.dumps({'warm_client_process_ms':{'p50':statistics.median(samples),'p95':ordered[23],'max':max(samples)}}))
finally:
    service.terminate()
    service.communicate(timeout=3)
print('PASS: absent, stereo, plain, PNG, identity invalidation and warm queries')
