"""Verify group ghosts, precision controls, locks, duplication and floor picking."""
import argparse
import json
import math
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
        raise RuntimeError('Editor tool check did not settle')
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def stable():
        return wait(lambda:(lambda r:r if r['inside'] and not any(i['waiting'] for i in r['items']) else None)(req('hideout read')))
    def group_items(group):return [i for i in stable()['items'] if i['group']==group]
    def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
    guard();req('panel 1');resolution=req('panel qa:resolution');req('panel 0');req('bossdoors 1');req('hideout go');base=stable();req('hideout edit 1')
    designs=json.loads((Path(__file__).resolve().parents[1]/'ui/hideout-designs.json').read_text())
    fern=next(m['index'] for m in designs['models'] if m['name']=='Fern1Standard');ids=[]
    for x in (-3,3):
        add=req(f'hideout add {fern} {x} 3');stable();req(f'hideout testpoint {x} 3');req('hideout drop');ids.append(add['id'])
    current=stable();first,second=[next(i for i in current['items'] if i['id']==id) for id in ids];checks=[]
    undo=stable()['undoCount'];req('hideout options grid 1');req('hideout options angle 5');req('hideout options floors 1');assert stable()['undoCount']==undo
    req('hideout select '+str(first['id']));req('hideout select '+str(second['id'])+' toggle');assert stable()['selectionCount']==2
    req('hideout testeditkey group');g=next(i['group'] for i in stable()['items'] if i['id']==first['id']);assert g>0 and len(group_items(g))==2
    req('hideout selection clear');req('hideout select '+str(first['id']));assert stable()['selectionCount']==2
    old=group_items(g);req('hideout selection move');req('hideout testpoint 3 0')
    wait(lambda:req('hideout placementread')['ghost'] and len(req('hideout placementread')['members'])==1)
    rotation=req('hideout testrotation 2 2 1000');assert rotation['degrees']==5
    req('hideout testpoint 3 0');preview=req('hideout placementread');assert preview['members'][0]['ghost']
    assert [(i['right'],i['up']) for i in group_items(g)]==[(i['right'],i['up']) for i in old]
    shot('group-preview');before=stable()['undoCount'];req('hideout drop');moved=group_items(g);assert stable()['undoCount']==before+1
    req('hideout testeditkey undo');restored=group_items(g);assert [(i['right'],i['up']) for i in restored]==[(i['right'],i['up']) for i in old]
    req('hideout testeditkey redo');assert len(group_items(g))==2;checks.append('Ctrl-selection, persistent groups, two green ghosts and one-step group history')
    anchor=group_items(g)[0]['id'];req('hideout select '+str(anchor));req('hideout selection lock 1');assert all(i['locked'] for i in group_items(g))
    bad=json.loads(le.send('hideout hold '+str(anchor),timeout=12,expected_process=p));assert not bad['ok']
    req('hideout objectlock '+str(anchor)+' 0');assert not any(i['locked'] for i in group_items(g));checks.append('Locks block moving and can be unlocked from the object list')
    before=stable();req('hideout testeditkey duplicate');pending=stable();copies=[i for i in pending['items'] if i['selected']]
    assert len(pending['items'])==len(before['items'])+2 and len(copies)==2 and all(i['group']!=g for i in copies)
    req('hideout testpoint 9 1');wait(lambda:req('hideout placementread')['ghost'] and req('hideout placementread')['members'][0]['ghost'])
    for command in ('hideout testeditkey group','hideout testeditkey ungroup','hideout selection group','hideout selection ungroup','hideout selection lock 1','hideout selection align x','hideout objectlock '+str(copies[0]['id'])+' 1'):
        bad=json.loads(le.send(command,timeout=12,expected_process=p));assert not bad['ok'],(command,bad)
        assert stable()['held']==pending['held'] and len(stable()['items'])==len(pending['items']) and req('hideout placementread')['ghost']
    req('hideout testmenus click:hideout-hud-tools');wait(lambda:req('panel read')['dialog'],15)
    for command in ('panel qa:dialogclick:hideout-tools-group','panel qa:dialogclick:hideout-tools-lock','panel qa:dialogclick:hideout-tools-align-x'):
        bad=json.loads(le.send(command,timeout=12,expected_process=p));assert not bad['ok'],(command,bad)
    req('panel dismiss');req('panel 0')
    req('hideout at '+str(pending['held'])+' 8 1');assert req('hideout placementread')['right']==8
    req('hideout move '+str(pending['held'])+' 1 0');assert req('hideout placementread')['right']==9
    req('hideout here '+str(pending['held']));assert stable()['held']==pending['held']
    req('hideout selection move');assert stable()['held']==pending['held']
    checks.append('Pending copies survive blocked Group/Ungroup/Align/Lock, disabled Tools controls and legacy at/move/here')
    req('hideout testpoint 9 1');req('hideout drop');after=stable();assert after['undoCount']==before['undoCount']+1
    req('hideout testeditkey undo');assert len(stable()['items'])==len(before['items']);req('hideout testeditkey redo');checks.append('Ctrl+D duplicates a group as a single undoable placement')
    current=stable();copy_group=max(i['group'] for i in current['items']);req('hideout select '+str(group_items(copy_group)[0]['id']))
    req('hideout selection move');req('hideout testpoint 9 1')
    wait(lambda:req('hideout hudaudit')['visible'])
    req('hideout testmenus click:hideout-hud-tools');wait(lambda:req('panel read')['dialog'],15)
    req('panel qa:field:hideout-exact-0=9.5');req('panel qa:field:hideout-exact-1=1.25')
    req('panel qa:dialogclick:hideout-exact-apply');wait(lambda:not req('panel read')['shown'],15)
    preview=req('hideout placementread');assert abs(preview['right']-9.5)<.001 and abs(preview['up']-1.25)<.001
    req('hideout drop');checks.append('Actual precision dialog inputs update the group preview')
    # A minimal file isolates floor picking from tree canopies above the floor.
    req('hideout selection clear');source=(folder/'hideout-test.txt').read_text().splitlines();floor=next(line for line in source[2:] if len(line.split('\t'))>12 and ('Road' in line.split('\t')[9] or 'Floor' in line.split('\t')[9]))
    parts=floor.split('\t');parts[2:4]=['0','0'];minimal=source[:2]+[line for line in source[2:] if line.split('\t')[1] in ('-3','-4','-5','-6')]+['\t'.join(parts)]
    templates=folder/'hideout-templates-test';templates.mkdir(exist_ok=True);(templates/'Floor Pick Check.txt').write_text('\n'.join(minimal)+'\n')
    req('hideout template load Floor Pick Check');floor_item=next(i for i in stable()['items'] if i['walkable'])
    req('hideout options floors 0');assert req('hideout testpick '+str(floor_item['id']))['picked']!=floor_item['id']
    req('hideout options floors 1');assert req('hideout testpick '+str(floor_item['id']))['picked']==floor_item['id'];req('hideout undo');stable()
    checks.append('Floor picking toggle uses the actual ray picker; existing floor parts can be selected')
    req('hideout selection clear');req('hideout select '+str(group_items(g)[0]['id']));req('hideout selection move');req('hideout testpoint 3 0')
    try:
        for width,height in ((1280,720),(1920,1080),(2560,1440)):
            guard();req('panel 1');req(f'panel qa:resolution:{width}:{height}:3');req('panel 0')
            wait(lambda:(lambda r:r['built'] and not r['jobs'] and r['screen']==[width,height])(req('panel read')),40)
            a=wait(lambda:(lambda a:a if a['visible'] else None)(req('hideout hudaudit')),10);assert a['onScreen'] and not a['outsideButtons'] and not a['overlappingButtons'],a
            shot(f'group-toolbar-{width}-{height}');req('hideout testmenus click:hideout-hud-tools');wait(lambda:req('panel read')['dialog'],15);shot(f'editor-tools-{width}-{height}')
            req('panel qa:dialogclick:hideout-tools-back');wait(lambda:not req('panel read')['shown'],15)
    finally:
        req('hideout unhold');req('hideout edit 0');req('panel 1');req(f"panel qa:resolution:{resolution['width']}:{resolution['height']}:{resolution['mode']}");req('panel 0')
    checks.append('Group toolbar and precision dialog fit 720p, 1080p and 1440p')
    original=group_items(g);req('hideout select '+str(original[0]['id']));req('hideout selection align x');aligned=group_items(g)
    assert abs(aligned[0]['right']-aligned[1]['right'])<.01
    req('hideout testeditkey undo');restored=group_items(g);assert [(i['right'],i['up']) for i in restored]==[(i['right'],i['up']) for i in original]
    req('hideout select '+str(restored[0]['id']));req('hideout testeditkey ungroup');assert not any(i['group']==g for i in stable()['items']);req('hideout testeditkey undo');assert len(group_items(g))==2
    req('hideout edit 0');checks.append('Measured alignment and Ctrl+Shift+G ungroup both restore through Undo')
    before=stable();req('hideout edit 1');added=req(f'hideout add {fern} 3 0');stable();req('hideout at '+str(added['id'])+' 3 0');req('hideout turn '+str(added['id'])+' 30');assert req('hideout placementread')['ghost'] and req('hideout placementread')['valid']
    req('hideout cancel');after=stable();assert not after['editing'] and after['held']<0 and not req('hideout placementread')['ghost'];assert len(after['items'])==len(before['items'])
    fingerprint=lambda r:sorted((i['name'],i['right'],i['up'],i['lift'],i['yaw'],i['size'],i['group'],i['locked'],i['off']) for i in r['items'])
    assert fingerprint(before)==fingerprint(after);checks.append('Cancel disposes a valid pending preview before restoring IDs and the exact layout')
    r=req('panel read');assert not r['faults'] and not r['softFaults'],r['error']
    result=dict(passed=True,checks=checks);(folder/'tools-checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
