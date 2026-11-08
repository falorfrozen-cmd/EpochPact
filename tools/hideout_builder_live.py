"""Focused real Builder actions; requires an owned EpFeedbackTest session."""
import argparse
import json
from pathlib import Path
import time
from tools import le_session as le


def run(folder):
    path=folder/'session.json';s=json.loads(path.read_text());p=s['process'];checks=[]
    def req(c):
        assert s['phase']=='running' and le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
        r=json.loads(le.send(c,timeout=15,expected_process=p));assert r.get('ok'),(c,r);return r
    def wait(fn,sec=20):
        end=time.monotonic()+sec
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.15)
        raise AssertionError('Timed out')
    def read():return req('hideout read')
    def click(name):return req('panel qa:dialogclick:hideout-builder-'+name)
    def field(text):
        req('panel qa:field:hideout-builder-search='+text.replace(' ','%20'));time.sleep(.6)
    def passed(text):
        checks.append(text);(folder/'builder-checks.json').write_text(json.dumps({'checks':checks},indent=2));print(text,flush=True)
    ident=req('identityread');assert ident['offline'] and ident['player']['name']=='EpFeedbackTest' and str(ident['player']['id'])==s['cloneId']
    req('panel 0');req('hideout testmenus close');req('hideout go');wait(lambda:read()['inside'],90)
    req('hideout edit 1');r=read();a,b=[i for i in r['items'] if i['door']>0][:2]
    req('hideout select '+str(a['id']));req('hideout select '+str(b['id'])+' toggle');assert read()['selectionCount']==2
    req('hideout testmenus builder-open');wait(lambda:req('panel read')['standaloneDialog']);click('placed');field('Boss door')
    click('move-'+str(a['id']));wait(lambda:not req('panel read')['shown']);r=read()
    assert r['held']==a['id'] and r['selectionCount']==1 and not next(i for i in r['items'] if i['id']==b['id'])['selected']
    req('hideout unhold');passed('Placed-row Move narrows unrelated Ctrl-selection to that door only')
    req('hideout select '+str(a['id']));req('hideout select '+str(b['id'])+' toggle')
    req('hideout testmenus builder-open');wait(lambda:req('panel read')['standaloneDialog']);field('Boss door')
    before=req('bosstest '+s['cloneId']+' halllayout');click('remove-'+str(a['id']));wait(lambda:next(i for i in read()['items'] if i['id']==a['id'])['off'])
    r=read();assert not next(i for i in r['items'] if i['id']==b['id'])['off'];assert req('panel read')['standaloneDialog']
    passed('Placed-row Delete removes only target and leaves Builder open')
    click('undo');wait(lambda:not next(i for i in read()['items'] if i['id']==a['id'])['off']);wait(lambda:req('hideout hudaudit')['builder']['open']);time.sleep(.6)
    after=req('bosstest '+s['cloneId']+' halllayout');assert before['meshInstance']==after['meshInstance']
    click('redo');wait(lambda:next(i for i in read()['items'] if i['id']==a['id'])['off']);wait(lambda:req('hideout hudaudit')['builder']['open']);time.sleep(.6)
    click('undo');wait(lambda:not next(i for i in read()['items'] if i['id']==a['id'])['off']);time.sleep(.6)
    passed('Real Builder Undo/Redo retain dialog and terrain through door deletion/restoration')
    click('library');field('Crystal');wait(lambda:req('hideout hudaudit')['builder']['thumbnails']>0)
    click('inspect-primary');wait(lambda:not req('panel read')['shown']);r=read();pending=[i for i in r['items'] if i['pending']];assert len(pending)==1;pending_id=pending[0]['id'];history=r['undoCount']
    req('hideout testmenus builder-open');wait(lambda:req('panel read')['standaloneDialog']);click('placed');field('');r=read();pending=next(i for i in r['items'] if i['id']==pending_id)
    # New objects are appended; locate their page with the ordinary Next control.
    for _ in range(100):
        raw=json.loads(le.send('panel qa:dialogclick:hideout-builder-remove-'+str(pending_id),timeout=15,expected_process=p))
        if raw.get('ok'):break
        click('next');time.sleep(.15)
    else:raise AssertionError('Pending object row not found')
    wait(lambda:all(i['id']!=pending_id for i in read()['items']));assert read()['undoCount']==history
    passed('Deleting an unplaced added model cancels its ghost without a phantom history edit')
    click('library');field('Statue');req('panel layout:125');wait(lambda:req('hideout hudaudit')['builder']['open'],90);assert req('hideout hudaudit')['builder']['query']=='Statue';assert req('panel read')['standaloneDialog']
    req('panel layout:100');wait(lambda:req('hideout hudaudit')['builder']['open'],90);assert req('hideout hudaudit')['builder']['query']=='Statue'
    passed('Changing text scale rebuilds and reopens dedicated Builder with its search preserved')
    click('close');req('hideout cancel');v=req('panel read');assert not v['faults'] and not v['softFaults']
    (folder/'builder-checks.json').write_text(json.dumps({'passed':True,'checks':checks},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
