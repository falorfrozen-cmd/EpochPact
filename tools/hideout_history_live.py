"""Verify deletion and real editor-frame Undo/Redo on an owned test clone."""
import argparse
import json
import os
import stat
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
        raise RuntimeError('History test did not settle')
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def ready():
        r=req('hideout read');return r if r['inside'] and not any(i['waiting'] for i in r['items']) else None
    def stable():return wait(ready)
    def fingerprint(r):
        rows=[]
        for i in r['items']:
            rows.append(tuple([i['kind'],i['door'],i['name'],i['off']]+[round(i[k],3) for k in ('right','up','lift','yaw','size')]))
        return [r['shape'],r['size'],r['texture'],sorted(rows)]
    def model():return next(i for i in stable()['items'] if i['name']==model_name)
    def history(key):req('hideout testeditkey '+key);return stable()
    guard();req('panel 0');req('bossdoors 1');req('hideout go');base=stable();req('hideout edit 1')
    designs=json.loads((Path(__file__).resolve().parents[1]/'ui/hideout-designs.json').read_text())
    original_names={i['name'] for i in base['items']}
    chosen=next(m for m in designs['models'] if m['name']=='Grass_Flower_PinkWhite_Clump' and m['name'] not in original_names)
    index=chosen['index'];model_name=chosen['name']
    count=len(base['items']);checks=[]
    add=req(f'hideout add {index} 3 0');stable();req('hideout testpoint 3 0');req('hideout drop');added=stable()
    assert len(added['items'])==count+1 and added['canUndo'];checks.append('Committed addition creates one undoable edit')
    req('hideout hold '+str(model()['id']))
    wait(lambda:any(b['name']=='hideout-hud-delete' for b in req('hideout hudaudit').get('buttons',[])),10)
    capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/'delete-toolbar.png')
    req('hideout testmenus click:hideout-hud-delete')
    wait(lambda:(lambda r:len(r['items'])==count and r['held']<0)(req('hideout read')),10)
    assert not req('hideout placementread')['ghost'];checks.append('Visible Delete button removes original, selection and green ghost')
    r=history('undo');assert fingerprint(r)==fingerprint(added) and r['canRedo']
    r=history('redo');assert len(r['items'])==count
    r=history('undo');assert fingerprint(r)==fingerprint(added);checks.append('Ctrl+Z / Ctrl+Y execute through EditFrame while pointer is over the toolbar')
    snapshots=[fingerprint(r)]
    for command in ('turn {id} 30','size {id} 1.2','move {id} 1 0'):
        guard();req('hideout '+command.format(id=model()['id']));r=stable();snapshots.append(fingerprint(r))
    assert not r['canRedo'];checks.append('New edit discards the old redo branch')
    for expected in snapshots[-2::-1]:assert fingerprint(history('undo'))==expected
    for expected in snapshots[1:]:assert fingerprint(history('redo'))==expected
    checks.append('Three distinct transforms undo and redo in order')
    before=stable();target=folder/'hideout-test.txt'
    try:
        os.chmod(target,stat.S_IREAD)
        bad=json.loads(le.send('hideout undo',timeout=12,expected_process=p));assert not bad['ok'],bad
    finally:os.chmod(target,stat.S_IREAD|stat.S_IWRITE)
    after=stable();assert fingerprint(after)==fingerprint(before) and after['undoCount']==before['undoCount']
    checks.append('Failed persistence restores the visible layout without advancing history')
    before=stable()
    try:
        os.chmod(target,stat.S_IREAD)
        bad=json.loads(le.send('hideout remove '+str(model()['id']),timeout=12,expected_process=p));assert not bad['ok'],bad
    finally:os.chmod(target,stat.S_IREAD|stat.S_IWRITE)
    after=stable();assert fingerprint(after)==fingerprint(before) and after['undoCount']==before['undoCount']
    checks.append('Failed deletion persistence restores the deleted model and history head')
    req('hideout hold '+str(model()['id']));before=stable();req('hideout testeditkey delete ui')
    assert len(req('hideout read')['items'])==len(before['items']);req('hideout testeditkey delete')
    assert len(stable()['items'])==count and not req('hideout placementread')['ghost']
    history('undo');checks.append('Native UI blocks Delete; pointer-only HUD blocking does not')
    req('hideout hold '+str(model()['id']));req('hideout remove '+str(model()['id']))
    assert stable()['held']<0 and not req('hideout placementread')['ghost'];history('undo')
    checks.append('F6 remove path also cleans held selection')
    before=fingerprint(stable());req('hideout reset');stable();assert fingerprint(history('undo'))==before
    reset=fingerprint(history('redo'));assert len(reset[-1])==count;history('undo')
    req('hideout template ready 2');stable();assert fingerprint(history('undo'))==before
    checks.append('Reset and template replacement remain recoverable')
    exit_item=next(i for i in stable()['items'] if i['door']==-3);req('hideout hold '+str(exit_item['id']))
    a=wait(lambda:(lambda a:a if a['held']==exit_item['id'] else None)(req('hideout hudaudit')),10);assert not any(b['name']=='hideout-hud-delete' for b in a['buttons'])
    bad=json.loads(le.send('hideout remove '+str(exit_item['id']),timeout=12,expected_process=p));assert not bad['ok']
    req('hideout unhold');checks.append('Protected exit cannot be deleted')
    before=stable();req(f'hideout add {index} 4 0');stable();req('hideout testeditkey delete')
    after=stable();assert fingerprint(before)==fingerprint(after) and before['undoCount']==after['undoCount']
    assert not req('hideout placementread')['ghost'];checks.append('Deleting an uncommitted addition leaves no phantom history or ghost')
    req('hideout edit 0');r=req('panel read');assert not r['faults'] and not r['softFaults'],r['error']
    result=dict(passed=True,checks=checks,undoCount=after['undoCount'],redoCount=after['redoCount'])
    (folder/'history-checks.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
