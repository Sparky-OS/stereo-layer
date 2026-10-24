#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('fixtures', type=Path)
p.add_argument('--probe')
a = p.parse_args()
failures = 0
for row in json.loads((a.fixtures/'manifest.json').read_text()):
    path = a.fixtures/row['file']
    if a.probe:
        result = subprocess.run([a.probe, '--base', row['base'], '--json', str(path)], capture_output=True, text=True)
        actual = json.loads(result.stdout) if result.returncode == 0 else {'error': result.stderr}
        expected = {k: row[k] for k in ['stereo', 'layout', 'order']}
        matched = all(actual.get(k) == v for k, v in expected.items())
    else:
        result = subprocess.run(['gio', 'info', '-a', 'standard::content-type', str(path)], capture_output=True, text=True)
        actual = result.stdout.split('standard::content-type: ')[-1].strip()
        expected = ('video/x-matroska-3d' if row['base'].startswith('video/') else 'image/x-png-3d') if row['stereo'] == 1 else row['base']
        matched = actual == expected
    print(('PASS' if matched else 'FAIL'), row['file'], 'expected', expected, 'actual', actual)
    failures += not matched
print(f'{failures} failures')
sys.exit(bool(failures))
