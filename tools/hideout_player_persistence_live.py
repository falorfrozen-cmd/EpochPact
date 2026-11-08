"""Normal ZIP runtime: commit, restart and reload local editor preferences/layout."""
import argparse,hashlib,json,time
from pathlib import Path
from tools import le_session as le,ingame_live_session as session

def run(folder):
 def state():return json.loads((folder/'session.json').read_text())
 s=state();assert s['phase']=='running'
 def req(c):
  current=state();assert le.process_identity(current['pid'])==current['process']
  r=json.loads(le.send(c,timeout=15,expected_process=current['process']));assert r.get('ok'),(c,r);return r
 def wait(fn,seconds=100):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   r=fn()
   if r:return r
   time.sleep(.2)
  raise RuntimeError('Player persistence did not settle')
 def ready():return wait(lambda:(lambda r:r if r['inside'] and not any(i['waiting'] for i in r['items']) else None)(req('hideout read')))
 def identity():
  r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==state()['cloneId']
 identity();req('panel 0');req('bossdoors 1');req('hideout go');before=ready();req('hideout edit 1')
 req('hideout options grid 0.5');req('hideout options angle 5');req('hideout favorite 1240 1')
 added=req('hideout add 1240 3 0');ready();req('hideout at '+str(added['id'])+' 3 0');req('hideout turn '+str(added['id'])+' 20');req('hideout drop');after=ready();assert len(after['items'])==len(before['items'])+1
 req('hideout template save Player Roundtrip');req('hideout edit 0')
 files={name:hashlib.sha256((folder/name).read_bytes()).hexdigest() for name in ('hideout.txt','hideout.txt.library','hideout.txt.editor','hideout-templates/Player Roundtrip.txt')}
 session.restart(folder);identity();req('panel 0');req('bossdoors 1');req('hideout go');r=ready()
 assert r['grid']==.5 and r['angle']==5 and len(r['items'])==len(after['items']);assert any(i['name']=='Fern1Standard' and i['yaw']==20 and i['right']==3 for i in r['items'])
 assert any(i['i']==1240 for i in req('hideout browse favorites az 0')['rows']);assert req('hideout browse recent recent 0')['rows'][0]['i']==1240
 for name,digest in files.items():assert hashlib.sha256((folder/name).read_bytes()).hexdigest()==digest,name
 key=next(d['preview'] for d in r['savedDetails'] if d['name']=='Player Roundtrip');req('panel 1');req('hideout preview '+key);wait(lambda:req('hideout preview '+key)['ready']);req('panel 0')
 panel=req('panel read');assert not panel['faults'] and not panel['softFaults'],panel['error']
 result=dict(passed=True,checks=['Normal player API add/at/rotate/drop creates a durable edit','Normal close/restart retains layout, grid, angle, favorites and recent models byte-for-byte','Saved personal template reloads and renders in normal player runtime'],files=files)
 (folder/'player-persistence-checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);run(a.parse_args().folder.resolve())
