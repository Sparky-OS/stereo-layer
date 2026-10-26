#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
"""Generate the shared MIME catalogue and detector mapping from the same data."""
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

NS = 'http://www.freedesktop.org/standards/shared-mime-info'
LANG = '{http://www.w3.org/XML/1998/namespace}lang'
ET.register_namespace('', NS)
variants = {
    '3d': ('stereo', ['']),
    'spherical': ('spherical', ['']),
    '360': ('spherical', ['spherical']),
    '180': ('spherical', ['spherical']),
    '3d-360': ('stereo_spherical', ['3d','360']),
    '3d-180': ('stereo_spherical', ['3d','180']),
    '3d-spherical': ('stereo_spherical', ['3d','spherical']),
}

def generate(catalogue, output):
    output.mkdir(parents=True, exist_ok=True)
    root = ET.Element(f'{{{NS}}}mime-info')
    mapping = []
    lists = {'video': [], 'picture': [], 'stereo_video': [], 'stereo_picture': []}
    for family in catalogue['families']:
        if not family['enabled']:
            continue
        stem, base = family['stem'], family['base']
        for alias in [base] + family.get('aliases', []):
            mapping.append(f'    {{"{alias}", "{stem}"}},')
        for suffix, (label, parents) in variants.items():
            if family['kind'] == 'picture' and suffix != '3d':
                continue
            mime = stem + '-' + suffix
            node = ET.SubElement(root, f'{{{NS}}}mime-type', {'type': mime})
            for lang, templates in catalogue['comments'][family['kind']].items():
                text = templates[label].format(format=family['label'])
                if suffix.endswith(('180','360')):
                    text += ' (' + suffix[-3:] + '°)'
                ET.SubElement(node, f'{{{NS}}}comment', {} if lang == 'en' else {LANG:lang}).text = text
            direct = [base if parent == '' else stem + '-' + parent for parent in parents]
            if mime == 'video/x-matroska-3d':
                direct.insert(0, 'application/x-matroska')
            for parent in direct:
                ET.SubElement(node, f'{{{NS}}}sub-class-of', {'type':parent})
            lists[family['kind']].append(mime)
            if suffix.startswith('3d'):
                lists['stereo_' + family['kind']].append(mime)
    ET.indent(root)
    ET.ElementTree(root).write(output/'stereo-content.xml', encoding='utf-8', xml_declaration=True)
    (output/'content-mime-map.inc').write_text('/* Generated from data/content/catalogue.json. */\n' + '\n'.join(mapping) + '\n')
    (output/'mime-lists.json').write_text(json.dumps(lists, indent=2)+'\n')

if __name__ == '__main__':
    generate(json.loads(Path(sys.argv[1]).read_text()), Path(sys.argv[2]))
