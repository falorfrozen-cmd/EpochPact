"""Real-game regressions for service hit boxes, preview lifetime and F6 UI recovery."""
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
            r=fn()
            if r:return r
            time.sleep(.15)
        raise RuntimeError('Native regression did not settle')
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def loaded():
        r=req('hideout read');return r if r['inside'] and not any(i['waiting'] for i in r['items']) else None
    def hud():return req('hideout hudaudit')['visible']
    def faults():
        r=req('panel read');return r['faults'],r['softFaults']
    def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
    guard();req('panel 1');baseline=faults();assert baseline==(0,0)
    req('bossdoors 1');req('hideout go');wait(loaded);req('panel 0');req('hideout testmenus close');wait(hud)
    result={'templates':[],'panelCycles':0}
    for index in range(5):
        guard();req('hideout template ready '+str(index));wait(loaded);time.sleep(.7)
        checks=[]
        for action in (-4,-5,-6):
            r=req(f"bosstest {s['cloneId']} stationhit {action}")
            assert r['rayHitsVisibleModel'] and r['boundsError']<.02,r
            checks.append({'action':action,**r})
        for action,panel in ((-4,'stash'),(-5,'shop')):
            req(f"bosstest {s['cloneId']} stationhit {action} click")
            wait(lambda:req(f"qoltest {s['cloneId']} panels")[panel],20)
            if index==0:shot('model-click-'+panel)
            req('hideout testmenus close');wait(hud)
        result['templates'].append({'index':index,'stations':checks,'stashAndVendorOpen':True})
        print('station template',index,'passed',flush=True)
    req('panel 1');req('panel page:hideout');req('panel qa:click:hideout-tab-library');req('panel scroll:320')
    # More than the 32-entry RAM cache: repeatedly render/evict real textures.
    models=json.loads((Path(__file__).resolve().parents[1]/'ui/hideout-designs.json').read_text())['models']
    keys=['a:'+str(m['index']) for m in models]
    for key in keys:
        wait(lambda:req('hideout preview '+key)['ready'],30)
        assert req('panel read')['shown'] and faults()==baseline
    key=keys[-1]
    req('hideout testpreviewdestroy '+key);wait(lambda:req('hideout preview '+key)['ready'],30)
    req('hideout testpreviewevict '+key);wait(lambda:req('hideout preview '+key)['ready'],30)
    result['cache']={'rendered':len(keys),'destroyedTextureRecovered':True,'deadTextureEvictionPassed':True}
    for cycle in range(20):
        req('panel 0');wait(hud);req('panel 1');assert not hud()
        assert req('panel read')['shown'] and faults()==baseline
        result['panelCycles']+=1
    req('panel qa:test-window-reset');wait(hud)
    result['hudSurvivedWindowDestruction']=True
    req('panel 1');wait(lambda:(lambda r:r['built'] and r['jobs']==0)(req('panel read')))
    req('panel page:hideout');req('panel qa:click:hideout-tab-library');time.sleep(2)
    assert req('panel read')['shown'] and faults()==baseline
    req('panel 0');req('hideout testmenus close');wait(hud);shot('edit-button-restored')
    req('hideout edit 1');shot('editor-restored');req('hideout edit 0')
    result.update(passed=True,faults=list(faults()))
    (folder/'interaction-checks.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
