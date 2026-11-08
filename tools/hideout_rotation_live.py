"""Check editor-only Q/E rotation and native input restoration on the disposable clone.

The private fixture feeds key samples into the actual rotation action. It does
not synthesize desktop keyboard input or change the player's key bindings.
"""
import argparse
import json
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process']
    assert s['phase']=='running' and 'panelBuilt' in s
    assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    def req(c):
        r=json.loads(le.send(c,timeout=12,expected_process=p));assert r.get('ok'),(c,r);return r
    def wait(fn,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=fn()
            if value:return value
            time.sleep(.15)
        raise RuntimeError('Rotation check did not settle')
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def ready():
        r=req('hideout read');return r if r['inside'] and not any(i['waiting'] for i in r['items']) else None
    def shot(name):
        capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
    guard();req('panel 1');original=req('panel qa:resolution');req('panel 0')
    req('bossdoors 1');req('hideout go');wait(ready)
    baseline=req('hideout testrotation read');assert not baseline['editing'] and not baseline['inputDisabled'],baseline
    result={'baselineInputDisabled':baseline['inputDisabled'],'rotations':[]}
    req('hideout edit 1');assert req('hideout testrotation read')['inputDisabled']
    stash=next(i['id'] for i in req('hideout read')['items'] if i['name']=='Stash')
    try:
        req('hideout hold '+str(stash));req('hideout testpoint 3 0')
        for name,mask,degrees in [('Q',1,-15),('E',2,15),('Left',4,-15),('Right',8,15),('Q+E',3,0),('Q+Left',5,-15),('E+Right',10,15)]:
            guard();r=req(f'hideout testrotation {mask} {mask} 1000')
            assert r['degrees']==degrees and r['editing'] and r['inputDisabled'],r
            result['rotations'].append({'keys':name,'degrees':r['degrees']})
        req('panel 1');before=req('hideout testrotation read')['yaw']
        r=req('hideout testrotation 1 1 1001');assert r['degrees']==0 and r['yaw']==before and r['inputDisabled'],r
        result['f6BlocksRotation']=True;req('panel 0')
        req('hideout unhold');r=req('hideout testrotation 1 1 1002');assert r['degrees']==0 and r['inputDisabled'],r
        result['noSelectionDoesNotRotate']=True
        req('hideout hold '+str(stash));req('hideout testrotation 2 2 1003');req('hideout testpoint 3 0')
        result['toolbar']=[]
        for width,height in [(1280,720),(original['width'],original['height'])]:
            req('panel 1');req(f'panel qa:resolution:{width}:{height}:3');req('panel 0')
            def resized():
                r=req('panel read');return r if r['built'] and not r['jobs'] and r['screen']==[width,height] else None
            wait(resized,40)
            a=wait(lambda:(lambda a:a if a['visible'] else None)(req('hideout hudaudit')),10)
            assert a['onScreen'] and not a['outsideButtons'] and not a['overlappingButtons'],a
            result['toolbar'].append({'screen':[width,height],'audit':a});shot(f'qe-toolbar-{width}-{height}')
    finally:
        req('hideout cancel');req('panel 1')
        req(f"panel qa:resolution:{original['width']}:{original['height']}:{original['mode']}");req('panel 0')
    r=req('hideout testrotation read');assert not r['editing'] and r['inputDisabled']==baseline['inputDisabled'],r
    r=req('hideout testrotation 2 2 1004');assert r['degrees']==0 and not r['editing'] and not r['inputDisabled'],r
    result['normalInputRestored']=True
    r=req('panel read');assert not r['faults'] and not r['softFaults'],r['error']
    result['passed']=True
    (folder/'rotation-checks.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
