"""Exercise real saved-layout UI confirmations and gallery cards on a clone."""
import argparse,json,time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture

def run(folder):
 s=json.loads((folder/'session.json').read_text());p=s['process']
 assert s['phase']=='running' and le.process_identity(s['pid'])==p
 def req(c):
  r=json.loads(le.send(c,timeout=15,expected_process=p));assert r.get('ok'),(c,r);return r
 def wait(fn,seconds=60):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   r=fn()
   if r:return r
   time.sleep(.15)
  raise RuntimeError('Template UI did not settle')
 def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
 def click(name):
  wait(lambda:any(h['name']==name and h.get('enabled',True) and h.get('visible',True) for h in req('panel read')['hits']))
  req('panel qa:click:'+name)
 r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
 req('panel 1');original=req('panel qa:resolution')
 try:
  req('panel qa:resolution:1920:1080:3');wait(lambda:req('panel read')['screen']==[1920,1080]);req('panel page:hideout');req('panel qa:click:hideout-tab-templates');req('panel scroll:99999')
  wait(lambda:bool(req('hideout read')['savedDetails']));time.sleep(.8);shot('saved-template-gallery')
  names=[v['name'] for v in req('hideout read')['savedDetails']];first=names[0]
  req('panel qa:field:hideout-template-name='+first.replace(' ','%20'));click('hideout-template-save');wait(lambda:req('panel read')['dialog']);path=folder/'hideout-templates-test'/f'{first}.txt';before=path.read_bytes()
  req('panel dismiss');assert path.read_bytes()==before
  click('hideout-template-save');req('panel qa:confirm:hideout-template-save');wait(lambda:not req('panel read')['dialog']);assert path.exists()
  req('panel scroll:99999');click('hideout-saved-rename-0');wait(lambda:req('panel read')['dialog']);req('panel qa:field:hideout-rename-name=UI%20Renamed');req('panel qa:dialogclick:hideout-rename-apply')
  wait(lambda:(folder/'hideout-templates-test/UI Renamed.txt').exists());assert not path.exists()
  req('panel scroll:99999');names=[v['name'] for v in req('hideout read')['savedDetails']];index=names.index('UI Renamed');control='hideout-saved-delete-'+str(index)
  click(control);wait(lambda:req('panel read')['dialog']);shot('delete-template-confirm');req('panel dismiss');assert (folder/'hideout-templates-test/UI Renamed.txt').exists()
  click(control);req('panel qa:confirm:'+control);wait(lambda:not (folder/'hideout-templates-test/UI Renamed.txt').exists())
  req('panel qa:click:hideout-tab-library');req('panel qa:field:hideout-search=Fern1Standard');req('panel scroll:99999');time.sleep(.7)
  before=req('hideout browse favorites az 0 Fern1Standard')['total'];click('hideout-favorite-1240');wait(lambda:req('hideout browse favorites az 0 Fern1Standard')['total']!=before)
  req('panel qa:click:hideout-kind-favorites');time.sleep(.5);shot('favorite-library-ui');r=req('panel read');assert not r['faults'] and not r['softFaults'],r['error']
 finally:
  req(f"panel qa:resolution:{original['width']}:{original['height']}:{original['mode']}");req('panel 0')
 result=dict(passed=True,checks=['Actual overwrite Cancel and Confirm callbacks','Rename input and Apply on a saved card','Delete Cancel and Confirm target the captured name','Favorites button and native gallery UI at 1080p'])
 (folder/'template-ui-checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);run(a.parse_args().folder.resolve())
