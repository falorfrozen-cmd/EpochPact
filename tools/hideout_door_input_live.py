"""Probe actual owned-door callbacks on an isolated EpFeedbackTest session.

The private positive fixture supplies foreground ownership only around the
explicit callback. Negative background tests use the real Windows focus.
No desktop keyboard or mouse input is synthesized.
"""
import argparse
import ctypes
import json
import time
from pathlib import Path
from tools import le_session as le


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process'];prefix='bosstest '+s['cloneId']+' '
    assert 'panelBuilt' in s and s['phase']=='running'
    assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    def req(c):
        r=json.loads(le.send(c,timeout=12,expected_process=p));assert r.get('ok'),(c,r);return r
    def wait(fn,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.2)
        raise RuntimeError('Door input check did not settle')
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def state():return req(prefix+'doorstate')
    def click(index,kind='click'):return req(prefix+f'doorclick {index} {kind}')
    def ready():
        r=req('hideout read');return r if r['inside'] and not any(i['waiting'] for i in r['items']) else None
    guard();req('panel 0');req('bossdoors 1');req('hideout go');wait(ready);req('hideout testmenus close')
    original=state();doors=original['doors'];difficulty=next(k for k,d in enumerate(doors) if d['action']==0)
    assert original['pending']==-1 and not original['rush'] and all('Empowered' not in d['label'] for d in doors)
    result={'rejectedNonClickActions':[]}
    req(prefix+'uitrace on')
    for index,d in enumerate(doors):
        r=click(index,'nonclick');assert r['pending']==-1 and not r['rush'] and r['difficulty']==0,r
        result['rejectedNonClickActions'].append(d['action'])
    for kind in ('foreign','null'):
        r=click(difficulty,kind);assert r['difficulty']==0 and r['pending']==-1,r
    result['foreignAndNullSourcesRejected']=True
    user=ctypes.windll.user32;user.GetForegroundWindow.restype=ctypes.c_void_p
    user.GetWindowThreadProcessId.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong)]
    foreground=ctypes.c_ulong();user.GetWindowThreadProcessId(user.GetForegroundWindow(),ctypes.byref(foreground))
    assert foreground.value!=s['pid'],'This negative check needs the game actually in the background.'
    r=click(difficulty,'background');assert r['difficulty']==0,r;result['actualBackgroundRejected']=True
    req('panel 1');assert click(difficulty)['difficulty']==0;req('panel 0')
    req('hideout edit 1');assert click(difficulty)['difficulty']==0;req('hideout edit 0')
    result['f6AndEditingRejected']=True
    # No rejected input consumes the click's debounce: this succeeds immediately.
    assert click(difficulty)['difficulty']==1
    assert click(difficulty,'nonclick')['difficulty']==1
    time.sleep(.4);assert click(difficulty)['difficulty']==1 and state()['harbinger']
    time.sleep(.4);assert click(difficulty)['difficulty']==0 and not state()['harbinger']
    result['explicitLocalClickWorksWithoutMouseState']=True
    for action,panel in ((-4,'stash'),(-5,'shop')):
        guard();index=next(k for k,d in enumerate(state()['doors']) if d['action']==action)
        click(index);wait(lambda:req('qoltest '+s['cloneId']+' panels')[panel],15);req('hideout testmenus close')
    result['stashAndVendorStillWork']=True
    req(prefix+'uitrace clear');result['idle']=[]
    for _ in range(3):
        time.sleep(15);guard();r=state();panels=req('qoltest '+s['cloneId']+' panels')
        assert req('identityread')['player']['scene']=='MonolithHub' and r['pending']==-1 and not r['tracked'] and not r['rush'],r
        assert not panels['stash'] and not panels['shop'] and not panels['inventory'],panels
        result['idle'].append({'scene':'MonolithHub','pending':r['pending'],'unexpectedPanels':False})
    result['idleTrace']=req(prefix+'uitrace read');assert not result['idleTrace']['events'],result['idleTrace']
    # Positive test: the same guarded native callback still starts the requested boss.
    guard();req(prefix+'god');index=next(k for k,d in enumerate(state()['doors']) if d['action']==4)
    click(index)
    def arrived():
        r=json.loads(le.send('identityread',timeout=12,expected_process=p));player=r.get('player') or {}
        return r if r.get('ok') and r.get('offline') and player.get('name')=='EpFeedbackTest' and player.get('scene') not in (None,'MonolithHub') else None
    r=wait(arrived,120);result['explicitBossScene']=r['player']['scene'];assert state()['tracked']
    r=req('panel read');assert not r['faults'] and not r['softFaults'],r['error']
    result['passed']=True;(folder/'door-input-checks.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
