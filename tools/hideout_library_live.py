"""Owned-clone tests for saved previews, library preferences and placement safety."""
import argparse, hashlib, json, time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture

def run(folder):
    s=json.loads((folder/'session.json').read_text());p=s['process'];assert s['phase']=='running' and 'panelBuilt' in s
    assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
    def req(c,ok=True):
        r=json.loads(le.send(c,timeout=16,expected_process=p));assert bool(r.get('ok'))==ok,(c,r);return r
    def wait(fn,seconds=120):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.2)
        raise RuntimeError('Library/safety condition did not settle')
    def stable():return wait(lambda:(lambda r:r if r['inside'] and not any(i['waiting'] for i in r['items']) else None)(req('hideout read')))
    r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
    req('panel 0');req('bossdoors 1');req('hideout go');base=stable();req('hideout reset');stable();req('hideout edit 1');checks=[]
    designs=json.loads((Path(__file__).resolve().parents[1]/'ui/hideout-designs.json').read_text());fern=next(m['index'] for m in designs['models'] if m['name']=='Fern1Standard')
    req(f'hideout favorite {fern} 1');fav=req('hideout browse favorites az 0');assert any(i['i']==fern and i['favorite'] for i in fav['rows'])
    assert req('hideout browse tag:plant az 0 Fern')['total']>0
    assert req('hideout browse tag:column az 0 Garden')['total']>0
    assert req('hideout browse tag:floor az 0 Garden Pillar')['total']==0
    add=req(f'hideout add {fern} 3 3');stable();req('hideout testpoint 3 3');req('hideout drop');assert req('hideout browse recent recent 0')['rows'][0]['i']==fern
    req(f'hideout favorite {fern} 0');assert not req('hideout browse favorites az 0 Fern1Standard')['total'];req(f'hideout favorite {fern} 1')
    assert (folder/'hideout-test.txt.library').exists();checks.append('Favorites, recent order, persistence file and detailed name-derived filters')
    # Full model footprint must fit, even when its origin is on the ground.
    item=next(i for i in stable()['items'] if i['id']==add['id']);before=stable()['undoCount']
    req('hideout hold '+str(item['id']));req('hideout testpoint 25.95 0');placement=req('hideout placementread');assert not placement['valid'],placement
    req('hideout drop',False);assert stable()['undoCount']==before
    req('hideout unhold');req(f"hideout transform {item['id']} 25.95 0 0 0 100",False);assert stable()['undoCount']==before
    checks.append('Full-bounds rejection through mouse drop and precision commands leaves layout/history unchanged')
    # A deterministic native collider exercises the real game's NavMesh guard.
    add2=req(f'hideout add {fern} 0 0');stable();req('hideout testsafetybox '+str(add2['id']));req('hideout testpoint 0 0')
    rejected=req('hideout drop',False);assert any(word in rejected['error'].lower() for word in ('trap','path')),rejected
    assert not req('hideout placementread')['valid'];req('hideout unhold');assert stable()['undoCount']==before
    assert req('hideout audit')['unreachableStationsOrDoors']==0
    checks.append('Actual NavMesh rejects a solid obstruction, restores routes, keeps history unchanged')
    req('hideout template save Preview Check');r=stable();key=next(v['preview'] for v in r['savedDetails'] if v['name']=='Preview Check');layout=folder/'hideout-test.txt';old=layout.read_bytes()
    req('panel 1');req('panel page:hideout');req('hideout preview '+key)
    wait(lambda:req('hideout preview '+key)['ready']);assert layout.read_bytes()==old;assert stable()['items']==r['items']
    capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/'personal-template-preview.png')
    saved=(folder/'hideout-templates-test/Preview Check.txt');old_saved=saved.read_bytes()
    req('hideout template save preview check',False);assert saved.read_bytes()==old_saved
    req('hideout template rename Preview Check|Renamed Check');assert not saved.exists();req('hideout template save Collision Check')
    req('hideout template rename Renamed Check|Collision Check',False);assert (folder/'hideout-templates-test/Renamed Check.txt').exists()
    item=next(i for i in stable()['items'] if i['name']=='Fern1Standard');req(f"hideout turn {item['id']} 15");req('hideout template overwrite Renamed Check')
    r=stable();newkey=next(v['preview'] for v in r['savedDetails'] if v['name']=='Renamed Check');assert newkey!=key
    req('hideout preview '+newkey);wait(lambda:req('hideout preview '+newkey)['ready']);checks.append('Personal previews leave live layout untouched; overwrite/rename collisions protected; revision keys refresh')
    # Native UI builds with saved gallery and expanded library.
    req('panel page:hideout');req('panel qa:click:hideout-tab-library');req('panel scroll:320');time.sleep(.8)
    capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/'library-favorites.png')
    req('panel qa:click:hideout-tab-templates');req('panel scroll:0');time.sleep(.5)
    assert not req('panel read')['faults'];req('panel 0');req('hideout edit 0')
    result=dict(passed=True,checks=checks);(folder/'library-safety-checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
