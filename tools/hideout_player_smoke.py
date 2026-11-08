"""Smoke-check the normal player DLL in an owned, disposable offline session."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process']
    assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    assert hashlib.sha256((Path(s['game'])/'version.dll').read_bytes()).hexdigest()==s['loaderHash']
    checks=[]
    def req(command):
        r=json.loads(le.send(command,timeout=12,expected_process=p))
        assert r.get('ok'),(command,r)
        return r
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def until(fn):
        end=time.monotonic()+90
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.3)
        raise RuntimeError('Player smoke timed out')
    def ready():
        r=req('hideout read')
        return r if r['hall'] and not any(i['waiting'] for i in r['items']) else None
    def shot(name):capture(req,s['game'],folder/(name+'.png'))
    guard();checks.append(req('connectionping'));req('bossdoors 1')
    if req('identityread').get('player',{}).get('scene')!='MonolithHub':
        travel=le.send('bosstravel '+s['cloneId']+' MonolithHub:1',timeout=12,expected_process=p)
        assert travel.startswith('travel: normal waypoint transition requested'),travel
    until(lambda:req('identityread').get('player',{}).get('scene')=='MonolithHub');until(ready);req('hideout go')
    until(lambda:req('hideout read')['inside'])
    # Production must not contain the private renderer/UI fixtures.
    for command in ('hideout audit','hideout testeditkey delete','hideout testsafetybox 1','hideout testpick 1','panel qa:resolution','hideout testrotation read','bosstest '+s['cloneId']+' doorclick 0'):
        r=le.send(command,timeout=12,expected_process=p)
        try:assert not json.loads(r).get('ok'),r
        except json.JSONDecodeError:assert 'refused' in r or 'unknown' in r,r
    checks.append('Private hideout audit, rotation/door fixtures and UI QA commands absent from normal player DLL')
    for index in range(5):
        guard();req('hideout template ready '+str(index));r=until(ready);time.sleep(.5)
        assert not any(i['failed'] for i in r['items']);assert all(i['standing'] or i['kind']==2 or i['off'] for i in r['items'])
        req('hideout go');checks.append({'template':index,'objects':len(r['items']),'failed':0})
    req('hideout template ready 2');until(ready);req('hideout go');req('buildset zoom 2');time.sleep(1);shot('hideout-player-world');req('buildset zoom 1')
    # Production exposes normal page/scroll navigation, not private simulated
    # clicks. Inspect the default Templates tab; library input was covered in
    # the private renderer fixture using the same RawPicture/GalleryTick code.
    req('panel 1');req('panel page:hideout');req('panel scroll:460');time.sleep(.5);shot('player-thumbnails-loading')
    for index in (0,1):
        key='t:'+str(index);req('hideout preview '+key)
        until(lambda:req('hideout preview '+key)['ready'])
    time.sleep(.5);shot('player-thumbnails-ready');assert req('panel read')['faults']==0
    checks.append('Production template thumbnails render; initial loading wells and completed Ready captions captured')
    req('panel 0');guard();before=req('hideout read');req('hideout edit 1')
    data=json.loads((Path(__file__).resolve().parents[1]/'ui/hideout-designs.json').read_text(encoding='utf-8'))
    model=next(m for m in data['models'] if m['name']=='Fern1Standard')
    added=req('hideout add '+str(model['index'])+' 3 0');time.sleep(1);req('hideout turn '+str(added['id'])+' 30');req('hideout cancel');until(ready)
    assert len(req('hideout read')['items'])==len(before['items'])
    req('hideout edit 1');shot('player-edit-toolbar');req('hideout edit 0')
    checks.append('Production add/place/rotate/cancel and editing toolbar; no panel faults')
    r=req('hideout read');assert r['inside'] and not r['editing']
    assert (folder/'hideout.txt').is_file() and not (folder/'hideout-test.txt').exists()
    checks.append('Normal hideout.txt persistence path used inside temporary preferences folder')
    (folder/'player-smoke.json').write_text(json.dumps({'passed':True,'loaderHash':s['loaderHash'],'checks':checks},indent=2),encoding='utf-8')
    print(json.dumps(checks),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
