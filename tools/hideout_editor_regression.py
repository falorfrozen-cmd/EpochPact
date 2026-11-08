"""Extra hideout checks on the owned EpFeedbackTest clone, without desktop input."""
import argparse
import json
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder, ui_only=False):
    manifest=json.loads((folder/'session.json').read_text());process=manifest['process']
    assert le.game_pids()==[manifest['pid']] and le.process_identity(manifest['pid'])==process
    checks=[]
    def req(command):
        value=json.loads(le.send(command,timeout=12,expected_process=process))
        if not value.get('ok'):raise RuntimeError((command,value))
        return value
    def guard():
        identity=req('identityread')
        assert identity['offline'] and identity['player']['name']=='EpFeedbackTest' and str(identity['player']['id'])==manifest['cloneId']
    def ready():
        end=time.monotonic()+90
        while time.monotonic()<end:
            r=req('hideout read')
            if r['hall'] and not any(i['waiting'] for i in r['items']):
                time.sleep(1)
                return r
            time.sleep(.2)
        raise RuntimeError('Hideout did not settle')
    def same(r):
        # IDs are session-local handles, intentionally regenerated on load.
        fields=('name','door','right','up','lift','yaw','size','off')
        return (r['shape'],r['size'],r['texture'],[[i[k] for k in fields] for i in r['items']])
    def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),manifest['game'],folder/(name+'.png'))
    guard();req('bossdoors 1');req('qoltest '+manifest['cloneId']+' visit-MonolithHub:1')
    ready();req('hideout go');req('qoltest '+manifest['cloneId']+' panels-close')
    if not ui_only:
        for repeat in range(2):
            for index in range(5):
                guard();req('hideout template ready '+str(index));ready();req('hideout go')
                audit=req('hideout audit')
                assert not any(audit[k] for k in ('failed','pending','remainingScripts','unreachableStationsOrDoors','visualBoundsOutside')),(repeat,index,audit)
                print('repeat',repeat,'template',index,'passed',flush=True)
        checks.append('All five templates reapplied twice; 10 native model/script/bounds/NavMesh audits passed')
        original=same(req('hideout read'));req('hideout template save QA Saved Layout')
        req('hideout template ready 0');ready();req('hideout template load QA Saved Layout');restored=ready()
        assert same(restored)==original
        req('hideout template ready 1');ready();req('hideout template undo');assert same(ready())==original
        req('hideout template remove QA Saved Layout');assert 'QA Saved Layout' not in req('hideout read')['saved']
        checks.append('Named template save/load/delete and replacement undo preserve the exact transforms')
    req('panel 1');req('panel page:hideout');req('panel qa:click:hideout-tab-library');req('panel qa:field:hideout-search=Crystal')
    time.sleep(.8);req('panel scroll:520');time.sleep(2);shot('library-visible')
    state=req('panel read');assert not state['faults']
    candidates=[h['name'] for h in state['hits'] if h.get('name','').startswith('hideout-preview-a:')]
    assert candidates;req('panel qa:click:'+candidates[0]);time.sleep(2);shot('library-visible-inspection');req('panel dismiss')
    resolution=req('panel qa:resolution')
    try:
        for width,height,scale in ((1280,720,150),(1920,1080,100),(2560,1440,140)):
            req(f'panel qa:resolution:{width}:{height}:3');time.sleep(1)
            req('panel layout:'+str(scale));time.sleep(2);req('panel page:hideout');req('panel qa:click:hideout-tab-library');req('panel scroll:520');time.sleep(1)
            state=req('panel read');assert not state['faults']
            shot(f'library-{width}x{height}-{scale}')
            req('panel 0');req('hideout edit 1');shot(f'toolbar-{width}x{height}-{scale}');req('hideout edit 0');req('panel 1')
    finally:
        req(f"panel qa:resolution:{resolution['width']}:{resolution['height']}:{resolution['mode']}");req('panel layout:100');time.sleep(1)
    checks.append('Library, inspection and toolbar captured at 720p/150%, 1080p/100%, 1440p/140%; no panel faults')
    req('panel 0');req('hideout edit 0')
    (folder/'hideout-regression.json').write_text(json.dumps({'checks':checks,'panelFaults':req('panel read')['faults']},indent=2),encoding='utf-8')
    print(json.dumps(checks),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);parser.add_argument('--ui-only',action='store_true');args=parser.parse_args();run(args.folder.resolve(),args.ui_only)
