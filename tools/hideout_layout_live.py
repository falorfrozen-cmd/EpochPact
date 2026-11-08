"""Inspect real font-sized HUD controls and responsive hideout category rows."""
import argparse
import json
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process']
    assert s['phase']=='running' and le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    def req(command):
        r=json.loads(le.send(command,timeout=12,expected_process=p));assert r.get('ok'),(command,r);return r
    def wait(fn,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.15)
        raise RuntimeError('UI/layout did not settle')
    def panel_ready():
        r=req('panel read');return r if r['built'] and r['jobs']==0 else None
    def loaded():
        r=req('hideout read');return r if r['inside'] and not any(i['waiting'] for i in r['items']) else None
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
    def group(state,prefix):
        hits=[h for h in state['hits'] if h['name'].startswith(prefix)]
        assert hits,(prefix,state['page'])
        for k,a in enumerate(hits):
            assert a['x']>=0 and a['w']>0,(prefix,a)
            for b in hits[:k]:
                dx=min(a['x']+a['w'],b['x']+b['w'])-max(a['x'],b['x'])
                dy=min(a['y']+a['h'],b['y']+b['h'])-max(a['y'],b['y'])
                assert dx<=.01 or dy<=.01,(prefix,a,b)
        return hits
    guard();req('panel 1');resolution=req('panel qa:resolution');req('bossdoors 1');req('hideout go');wait(loaded)
    results=[]
    try:
        for width,height,scale in ((1280,720,150),(1920,1080,100),(2560,1440,140)):
            guard();req('panel 1');req(f'panel qa:resolution:{width}:{height}:3');time.sleep(1)
            req('panel layout:'+str(scale));wait(panel_ready);req('panel page:hideout');req('panel qa:click:hideout-tab-library');req('panel scroll:320');time.sleep(.4)
            state=wait(panel_ready);row={'resolution':[width,height],'textScale':scale,'categories':group(state,'hideout-kind-'),'tabs':group(state,'hideout-tab-')};results.append(row)
            assert state['faults']==0;shot(f'categories-{width}-{scale}')
            req('panel qa:click:hideout-tab-placed');req('panel scroll:320');wait(panel_ready);shot(f'placed-{width}-{scale}')
            req('panel 0');req('hideout testmenus close');req('hideout edit 1')
            wait(lambda:req('hideout hudaudit')['visible'],10)
            for mode in ('idle','held'):
                if mode=='held':
                    stash=next(i for i in req('hideout read')['items'] if i['name']=='Stash');req('hideout hold '+str(stash['id']))
                    wait(lambda:req('hideout hudaudit')['held']==stash['id'],10)
                audit=req('hideout hudaudit');row[mode]=audit
                assert audit['visible'] and audit['onScreen'] and not audit['outsideButtons'] and not audit['overlappingButtons'],audit
                assert audit['captionLines']<=1,audit
                shot(f'toolbar-{mode}-{width}-{scale}')
            req('hideout unhold');req('inventoryaction '+s['cloneId']+' stash')
            wait(lambda:req('qoltest '+s['cloneId']+' panels')['stash'],20)
            wait(lambda:(lambda a:a['visible'] and [b['name'] for b in a['buttons']]==['hideout-hud-edit'])(req('hideout hudaudit')),10)
            req('hideout testmenus edit-entry');wait(lambda:req('hideout hudaudit')['editing'],10)
            assert not req('qoltest '+s['cloneId']+' panels')['stash'];row['gameMenuCompactEntry']=True
            req('hideout edit 0');(folder/'layout-checks.json').write_text(json.dumps(results,indent=2));print(width,height,scale,'passed',flush=True)
    finally:
        req('hideout unhold');req('hideout edit 0');req('panel 1');req(f"panel qa:resolution:{resolution['width']}:{resolution['height']}:{resolution['mode']}");time.sleep(1);req('panel layout:100');wait(panel_ready);req('panel 0')
    print('All three responsive layout runs passed',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
