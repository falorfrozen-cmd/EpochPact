"""Exercise slow preview fairness, F6 click-through and automatic resolution changes."""
import argparse
import json
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process'];root=Path(__file__).resolve().parents[1]
    assert 'panelBuilt' in s and s['phase']=='running'
    assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    def req(c):
        r=json.loads(le.send(c,timeout=12,expected_process=p));assert r.get('ok'),(c,r);return r
    def wait(fn,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.12)
        raise RuntimeError('Regression state did not settle')
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def shot(n):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(n+'.png'))
    def loaded():
        r=req('hideout read');return r if r['inside'] and not any(i['waiting'] for i in r['items']) else None
    def ready(keys):
        q=req('hideout previewqueue');done={t['key'] for t in q['textures'] if t['ready']};return q if all(k in done for k in keys) else None
    guard();req('panel 1');original=req('panel qa:resolution');req('bossdoors 1');req('hideout go');wait(loaded)
    req('bosstest '+s['cloneId']+' uitrace on');req('hideout testmenus close')
    # Exercise the real native listener while the F6 window owns the UI.
    req('panel 1');req('bosstest '+s['cloneId']+' stationhit -4 click');time.sleep(.4)
    assert not req('qoltest '+s['cloneId']+' panels')['stash']
    req('panel 0');req('bosstest '+s['cloneId']+' stationhit -4 click')
    wait(lambda:req('qoltest '+s['cloneId']+' panels')['stash'],15);req('hideout testmenus close')
    result={'f6BlocksWorldClickThrough':True,'closedF6AllowsServiceClick':True}
    req('panel 1');req('panel page:general')
    models=json.loads((root/'ui/hideout-designs.json').read_text())['models'];keys=['a:'+str(m['index']) for m in models[:6]]
    assert not any(t['key'] in keys for t in req('hideout previewqueue')['textures']), 'Use a fresh session for the slow queue test'
    try:
        req('hideout testpreviewdelay 800');before=req('hideout previewqueue')['preemptions'];start=time.monotonic()
        for key in keys[:3]:req('hideout testpreviewqueue '+key)
        q=wait(lambda:ready(keys[:3]),15)
        assert q['preemptions']==before,q
        result['slowBackgroundQueue']={'models':3,'seconds':round(time.monotonic()-start,3),'preemptions':0}
        req('hideout testpreviewqueue '+keys[3]);wait(lambda:req('hideout previewqueue')['active']==keys[3],5)
        req('hideout testpreviewqueue '+keys[4]);req('hideout testpreviewqueue '+keys[5]+' priority')
        wait(lambda:req('hideout previewqueue')['active']==keys[5],5)
        q=wait(lambda:ready(keys[3:]),15);assert q['preemptions']>before,q
        result['hoverPriorityPreemptsBackground']=True
    finally:req('hideout testpreviewdelay 0')
    # No layout/text-scale command is used: screen changes must rebuild by themselves.
    req('panel 1');req('panel page:hideout');req('panel qa:click:hideout-tab-library')
    baseline=req('panel read');base_scale=baseline['scale'];req('hideout edit 1')
    stash=next(i['id'] for i in req('hideout read')['items'] if i['name']=='Stash');req('hideout hold '+str(stash))
    result['resolutions']=[]
    try:
        for width,height,shown in ((1280,720,False),(1920,1080,True),(original['width'],original['height'],False)):
            guard();req('panel 1');req(f'panel qa:resolution:{width}:{height}:3')
            if not shown:req('panel 0')
            expected=base_scale*height/original['height']
            def resized():
                r=req('panel read');return r if r['built'] and r['jobs']==0 and r['screen']==[width,height] and abs(r['scale']-expected)<.01 and r['shown']==shown else None
            r=wait(resized,40);h=req('hideout read');assert h['editing'] and h['held']==stash
            if not shown:
                a=wait(lambda:(lambda a:a if a['visible'] else None)(req('hideout hudaudit')),10)
                assert a['onScreen'] and not a['outsideButtons'] and not a['overlappingButtons'],a
            result['resolutions'].append({'screen':[width,height],'shown':shown,'scale':r['scale'],'selectionRetained':True})
            shot(f'auto-resize-{width}-{height}')
    finally:
        req('hideout unhold');req('hideout edit 0');req('panel 1')
        req(f"panel qa:resolution:{original['width']}:{original['height']}:{original['mode']}")
        wait(lambda:(lambda r:r['built'] and r['jobs']==0 and r['screen']==[original['width'],original['height']])(req('panel read')),40)
        req('panel 0');req('hideout testmenus close')
    result['trace']=req('bosstest '+s['cloneId']+' uitrace read')
    r=req('panel read');assert r['faults']==0 and r['softFaults']==0,r['error']
    result.update(passed=True,faults=[r['faults'],r['softFaults']]);(folder/'remaining-checks.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
