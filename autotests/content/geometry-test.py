#!/usr/bin/env python3
"""A caller needs native samples and sample aspect independently of packing."""
import json,subprocess,sys
from pathlib import Path
probe,fixtures=sys.argv[1:]
for name,expected in [('case-10',(1920,804,2,1,1,0)),('case-01',(320,180,1,1,1,1)),('case-02',(320,180,1,1,2,0)),('case-11',(960,804,1,1,0,0))]:
    result=json.loads(subprocess.check_output([probe,'--base','video/x-matroska','--json',str(Path(fixtures)/(name+'.mkv'))]))
    actual=tuple(result.get(k) for k in ('width','height','sar_num','sar_den','layout','order'))
    assert actual==expected,(name,actual,expected,result)
print('native sample dimensions, SAR and packing/order: four cases passed')
