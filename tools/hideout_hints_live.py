"""Final hint wrapping, library reload and saved-preview regression on the latest DLL."""
import argparse,json,time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture

def run(folder):
 s=json.loads((folder/'session.json').read_text());p=s['process'];assert s['phase']=='running' and 'panelBuilt' in s
 def req(c):
  r=json.loads(le.send(c,timeout=15,expected_process=p));assert r.get('ok'),(c,r);return r
 def wait(fn,seconds=90):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   r=fn()
   if r:return r
   time.sleep(.15)
  raise RuntimeError('Final hint check did not settle')
 def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),s['game'],folder/(name+'.png'))
 r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId']
 req('panel 1');resolution=req('panel qa:resolution');req('panel 0');req('bossdoors 1');req('hideout go')
 r=wait(lambda:(lambda r:r if r['inside'] and not any(i['waiting'] for i in r['items']) else None)(req('hideout read')))
 assert r['grid']==1 and r['angle']==5 and r['savedDetails'];assert req('hideout browse recent recent 0')['rows'][0]['n']=='Fern1Standard'
 req('hideout edit 1');i=next(i for i in r['items'] if i['name']=='Fern1Standard');req('hideout hold '+str(i['id']))
 try:
  for width,height in ((1280,720),(1920,1080),(2560,1440)):
   req('panel 1');req(f'panel qa:resolution:{width}:{height}:3');req('panel 0');wait(lambda:req('panel read')['screen']==[width,height]);req('hideout testpoint 25.95 0')
   a=wait(lambda:(lambda a:a if a['visible'] and a['hintLines']>=1 else None)(req('hideout hudaudit')),15)
   assert a['onScreen'] and not a['outsideButtons'] and not a['overlappingButtons'];shot(f'invalid-placement-{width}')
  req('hideout unhold');req('hideout edit 0');req('panel 1');key=req('hideout read')['savedDetails'][0]['preview'];req('hideout preview '+key);wait(lambda:req('hideout preview '+key)['ready']);req('panel page:hideout');req('panel scroll:99999');shot('saved-preview-reloaded')
 finally:
  req('hideout unhold');req('hideout edit 0');req('panel 1');req(f"panel qa:resolution:{resolution['width']}:{resolution['height']}:{resolution['mode']}");req('panel 0')
 r=req('panel read');assert not r['faults'] and not r['softFaults'],r['error']
 result=dict(passed=True,checks=['Grid, rotation, recent library and saved layouts reload in a fresh game process','Long invalid-placement hint and buttons fit 720p/1080p/1440p','Saved preview still renders with revised cache lookup'])
 (folder/'final-hints-checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);run(a.parse_args().folder.resolve())
