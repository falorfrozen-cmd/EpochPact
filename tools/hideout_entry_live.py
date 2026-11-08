"""Check the rendered edit/resume entry through native menus and F6 rebuilds."""
import argparse
import json
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process']
    assert 'panelBuilt' in s and s['phase']=='running'
    assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    def req(c):
        r=json.loads(le.send(c,timeout=12,expected_process=p));assert r.get('ok'),(c,r);return r
    def wait(fn,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.15)
        raise RuntimeError('Entry state did not settle')
    def compact():
        a=req('hideout hudaudit');return a if a['visible'] and [b['name'] for b in a['buttons']]==['hideout-hud-edit'] else None
    def expanded():
        a=req('hideout hudaudit');return a if a['visible'] and a['editing'] else None
    def shot(n):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(n+'.png'))
    identity=req('identityread');assert identity['offline'] and identity['player']['name']=='EpFeedbackTest' and str(identity['player']['id'])==s['cloneId']
    req('panel 1');req('bossdoors 1');req('hideout go');wait(lambda:req('hideout read')['inside'])
    req('panel 0');req('hideout testmenus close');wait(compact)
    req('inventoryaction '+s['cloneId']+' stash')
    wait(lambda:req('qoltest '+s['cloneId']+' panels')['stash'],20)
    wait(compact);shot('entry-over-native-menu')
    req('panel 1');wait(lambda:not req('hideout hudaudit')['visible'],10)
    req('panel 0');wait(compact)
    req('hideout testmenus edit-entry');wait(expanded)
    assert not req('qoltest '+s['cloneId']+' panels')['stash']
    assert req('hideout read')['editing']
    stash=next(i['id'] for i in req('hideout read')['items'] if i['name']=='Stash')
    req('hideout hold '+str(stash));wait(lambda:req('hideout hudaudit')['held']==stash,10)
    req('inventoryaction '+s['cloneId']+' stash')
    wait(lambda:req('qoltest '+s['cloneId']+' panels')['stash'],20)
    wait(compact);shot('resume-over-native-menu')
    req('hideout testmenus edit-entry');wait(expanded)
    assert not req('qoltest '+s['cloneId']+' panels')['stash']
    assert req('hideout read')['held']==stash
    for cycle in range(10):
        req('panel 1');wait(lambda:not req('hideout hudaudit')['visible'],10)
        req('panel 0');wait(expanded)
        assert req('hideout read')['held']==stash
    shot('selected-object-restored')
    req('hideout unhold');req('hideout edit 0');wait(compact)
    req('panel 1');req('panel qa:test-window-reset');wait(compact)
    req('panel 1');wait(lambda:(lambda r:r['built'] and r['jobs']==0)(req('panel read')))
    req('panel 0');wait(compact);shot('entry-after-window-rebuild')
    r=req('panel read');assert r['faults']==0 and r['softFaults']==0
    result={'passed':True,'nativeMenuEntryVisible':True,'entryClosesNativeMenusAndStartsEditor':True,'selectedObjectRetainedThroughResume':True,'panelCyclesWhileHolding':10,'entrySurvivedWindowRebuild':True,'faults':[r['faults'],r['softFaults']]}
    (folder/'entry-checks.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
