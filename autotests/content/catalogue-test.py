#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
xml = Path(sys.argv[1])
root = ET.parse(xml).getroot()
ns={'m':'http://www.freedesktop.org/standards/shared-mime-info'}
types={node.attrib['type']:node for node in root}
assert 'video/x-matroska-3d' in types
assert 'video/x-mp4-3d-180' in types, 'missing content-refined MP4 type'
assert not any('sparky' in name for name in types)
assert {n.attrib['type'] for n in types['video/x-matroska-3d'].findall('m:sub-class-of',ns)} == {'video/x-matroska','application/x-matroska'}
for name,node in types.items():
    languages={c.attrib.get('{http://www.w3.org/XML/1998/namespace}lang','en') for c in node.findall('m:comment',ns)}
    assert languages == {'en','pl','pt_BR','fr','de','ja','ko','zh_CN','zh_TW'}, name
    assert not node.findall('m:glob',ns), name
with tempfile.TemporaryDirectory(dir=xml.parent) as scratch:
    mime=Path(scratch)/'mime'
    (mime/'packages').mkdir(parents=True)
    (mime/'packages/content.xml').write_bytes(xml.read_bytes())
    result=subprocess.run(['update-mime-database',str(mime)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    edges=(mime/'subclasses').read_text().splitlines()
    for edge in ['video/x-matroska-3d application/x-matroska',
                 'video/x-matroska-3d video/x-matroska',
                 'video/x-mp4-3d-180 video/x-mp4-3d',
                 'video/x-mp4-3d-180 video/x-mp4-180']:
        assert edge in edges,edge
print(f'PASS: {len(types)} types, nine languages, preserved container ancestry, standard MIME compiler')
