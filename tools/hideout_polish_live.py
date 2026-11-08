"""Exercise stable door placement, in-place editor exit and dedicated builder on an owned clone."""
import argparse
import json
import math
import time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture


def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process'];checks=[]
    assert s['phase']=='running' and s.get('identity',{}).get('offline')
    def req(command):
        assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
        result=json.loads(le.send(command,timeout=15,expected_process=p))
        assert result.get('ok'),(command,result)
        return result
    def wait(fn,seconds=90):
        end=time.monotonic()+seconds;last=None
        while time.monotonic()<end:
            last=fn()
            if last:return last
            time.sleep(.15)
        raise AssertionError(('Timed out',last))
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    def stable():
        result=wait(lambda:(lambda r:r if r['inside'] and not any(i['waiting'] for i in r['items']) and all(i['standing'] or i['kind']==2 or i['off'] for i in r['items']) else None)(req('hideout read')))
        # The final async model-fit schedules a NavMesh bake 400ms later.
        time.sleep(.8)
        return result
    def hall():return req('bosstest '+s['cloneId']+' halllayout')
    def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
    def passed(text,**evidence):
        checks.append(dict(check=text,**evidence));(folder/'polish-checks.json').write_text(json.dumps(dict(checks=checks),indent=2),encoding='utf-8');print(text,flush=True)
    def same_instances(a,b):
        assert (a['hallInstance'],a['meshInstance'])==(b['hallInstance'],b['meshInstance'])
        assert {d['action']:d['instance'] for d in a['doors']}=={d['action']:d['instance'] for d in b['doors']}
    def poses(a):return {d['action']:(d['right'],d['up'],d['y'],d['listenerX'],d['listenerZ']) for d in a['doors']}
    def unchanged(a,b,except_action=None):
        aa,bb=poses(a),poses(b);assert aa.keys()==bb.keys()
        for key in aa:
            if key!=except_action:assert all(abs(x-y)<.002 for x,y in zip(aa[key],bb[key])),(key,aa[key],bb[key])
    def move_door(door):
        req('hideout hold '+str(door['id']))
        for dx,dz in ((.75,0),(-.75,0),(0,.75),(0,-.75)):
            req(f"hideout testpoint {door['right']+dx} {door['up']+dz}")
            wait(lambda:req('hideout placementread')['ghost'],10)
            r=req('hideout placementread')
            if not r['valid']:continue
            answer=json.loads(le.send('hideout drop',timeout=15,expected_process=p))
            if answer.get('ok'):time.sleep(.8);return dx,dz
        raise AssertionError('No safe nearby gate placement')
    guard();req('panel 0');req('hideout testmenus close');req('bossdoors 1');req('hideout go');stable()
    req('hideout template ready 0');stable();req('hideout go');time.sleep(1)
    original=hall();r=stable();door=next(i for i in r['items'] if i['door']==1)
    req('hideout edit 1');delta=move_door(door);after=hall();same_instances(original,after);unchanged(original,after,1)
    actual=next(d for d in after['doors'] if d['action']==1)
    assert math.dist((actual['right'],actual['up']),(door['right']+delta[0],door['up']+delta[1]))<.01
    passed('Moving one boss door preserves every unselected real pose and all native door instances',before=original,after=after)
    req('hideout edit 0');time.sleep(.8);done=hall();same_instances(after,done);unchanged(after,done)
    assert done['hallBuilds']==after['hallBuilds'] and done['doorBuilds']==after['doorBuilds']
    req('hideout edit 1');req('hideout cancel');time.sleep(.8);cancel=hall();same_instances(done,cancel);unchanged(done,cancel)
    passed('Done and unchanged Cancel retain floor mesh and door instances without rebuilding')
    req('hideout edit 1');door=next(i for i in stable()['items'] if i['door']==1);before=hall();audit=req('hideout audit')
    move_door(door);req('hideout cancel');stable();time.sleep(.8);after=hall();same_instances(before,after);unchanged(before,after)
    current=req('hideout audit');assert {i['id']:i.get('instance') for i in audit['items']}=={i['id']:i.get('instance') for i in current['items']}
    passed('Cancel restores a committed door move in place; floor and surviving decor instances stay unchanged')
    req('hideout testmenus close');req('hideout edit 1');wait(lambda:any(b['name']=='hideout-hud-library' for b in req('hideout hudaudit')['buttons']),10)
    before=(folder/'hideout-test.txt').read_bytes();req('hideout testmenus click:hideout-hud-library')
    view=wait(lambda:(lambda r:r if r.get('standaloneDialog') and r.get('dialog') else None)(req('panel read')),15)
    assert view['hiddenShellNodes']>0
    wait(lambda:req('hideout hudaudit')['builder']['thumbnails']>0,25);shot('dedicated-builder')
    assert (folder/'hideout-test.txt').read_bytes()==before
    passed('Add objects opens independent Hideout Builder; F6 chrome hidden, layout unchanged')
    req('panel qa:dialogclick:hideout-builder-search');wait(lambda:req('panel qa:caret:hideout-builder-search')['focused'],10)
    req('panel qa:field:hideout-builder-search=Crystal');wait(lambda:req('hideout hudaudit')['builder']['query']=='Crystal')
    time.sleep(1);assert req('panel qa:caret:hideout-builder-search')['focused'];shot('builder-search')
    passed('Debounced result refresh preserves the real search input focus/caret')
    candidates=req('hideout browse all az 0 Crystal')['rows']
    if candidates:
        index=str(candidates[0]['i']);req('panel qa:dialogclick:hideout-builder-preview-'+index);time.sleep(.4)
        assert req('hideout hudaudit')['builder']['inspected']
    req('panel qa:dialogclick:hideout-builder-placed');wait(lambda:req('hideout hudaudit')['builder']['placed']);shot('builder-placed')
    req('panel qa:dialogclick:hideout-builder-close');wait(lambda:not req('panel read')['shown'])
    assert stable()['editing']
    req('hideout testmenus builder-open');wait(lambda:req('panel read')['standaloneDialog']);req('panel 0')
    wait(lambda:not req('panel read')['shown']);assert not req('panel read')['standaloneDialog'] and not req('panel read')['gameKeysBlocked']
    req('panel 1');view=req('panel read');assert not view['standaloneDialog'] and not view['hiddenShellNodes'];req('panel 0')
    passed('Back to editing and F6 close restore focus; next F6 shows ordinary main window')
    req('hideout edit 0');exit_index=next(k for k,d in enumerate(hall()['doors']) if d['action']==-3)
    req('bosstest '+s['cloneId']+' doorclick '+str(exit_index));wait(lambda:not req('hideout read')['inside'])
    shortcut=wait(lambda:(lambda a:a if a['shortcut']['visible'] and a['shortcut']['enabled'] else None)(req('hideout hudaudit')),20)
    shot('minimap-hideout-shortcut');req('hideout testmenus shortcut-click');wait(lambda:req('hideout read')['inside'],90);stable()
    passed('Real minimap-bound shortcut travels from Hub to Hideout without opening F6',shortcut=shortcut['shortcut'])
    guard();view=req('panel read');assert not view['faults'] and not view['softFaults'],view['error']
    (folder/'polish-checks.json').write_text(json.dumps(dict(passed=True,checks=checks),indent=2),encoding='utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
