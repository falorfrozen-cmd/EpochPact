"""Exercise real template/saved thumbnail cache decode in an owned art-only session."""
import argparse,json,struct,time,hashlib,csv
from pathlib import Path
from tools import le_session as le

def run(folder,build):
 s=json.loads((folder/'session.json').read_text());assert s['phase']=='running' and s.get('artReviewOnly');p=s['process']
 def req(c):
  assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
  r=json.loads(le.send(c,timeout=15,expected_process=p));assert r.get('ok'),(c,r);return r
 assert req('sessionread')['state']=='Login'
 raw=(build/'ingame/hideout-designs.json').read_bytes();data=json.loads(raw)
 def fnv(b):
  h=1469598103934665603
  for byte in b:h=((h^byte)*1099511628211)&0xffffffffffffffff
  return h
 def file(key):return folder/'hideout-preview-cache'/f"{fnv(raw+key.encode()+b'26617preview-v4')}.rgba"
 def ready(key):
  req('hideout testpreviewqueue '+key+' priority');end=time.monotonic()+35
  while time.monotonic()<end:
   r=req('hideout preview '+key)
   if r['ready']:return r
   time.sleep(.12)
  raise AssertionError(r)
 checks=[];key='t:0';source=file(key);valid=source.read_bytes();assert struct.unpack('<4I',valid[:16])==(0x45505056,4,960,640)
 req('hideout testartclear');assert ready(key)['status']=='Ready';checks.append('960x640 template GPU texture evicted and reloaded from disk')
 for label,bad in [('wrong dimensions',struct.pack('<4I',0x45505056,4,480,320)+valid[16:]),('old cache version',struct.pack('<4I',0x45505056,3,960,640)+valid[16:]),('truncated cache',valid[:24])]:
  req('hideout testartclear');source.write_bytes(bad);r=ready(key);assert r['status'].startswith('Static preview:'),(label,r);assert struct.unpack('<4I',source.read_bytes()[:16])==(0x45505056,4,960,640) and len(source.read_bytes())==len(valid)
  checks.append(label+' rejected; thumbnail rendered again')
 name='Art Roundtrip';d=data['designs'][0];lines=['EpochPact hideout 1',f"ground\t1\t{d['size']}\t{d['texture']}"]
 for i in d['items']:
  values=[i['kind'],i['door'],i['right'],i['up'],i['lift'],i['yaw'],i['size'],0,i['guid'],i['name'],i['maxSpan'],1,int(i['walkable']),int(i.get('fit',i['maxSpan']>0)),0,0];lines.append('\t'.join(map(str,values)))
 target=folder/'hideout-templates-test'/f'{name}.txt';target.parent.mkdir(exist_ok=True);target.write_text('\n'.join(lines)+'\n',encoding='utf-8');before=target.read_bytes()
 key=f"s:{fnv(name.encode()+before):x}:"+name.encode().hex();r=ready(key);assert r['status'].startswith('Static preview:');assert struct.unpack('<4I',file(key).read_bytes()[:16])==(0x45505056,4,960,640);assert target.read_bytes()==before
 checks.append('Named saved layout parsed and rendered at 960x640; saved bytes unchanged; never applied to world')
 rows=[r for r in csv.reader((Path(__file__).resolve().parents[1]/'ui/hideout-catalog.tsv').open(encoding='utf-8'),delimiter='\t') if r and not r[0].startswith('#')];index=next(i for i,r in enumerate(rows) if r[3]=='CrystalCluster');key='a:'+str(index);r=ready(key);assert r['status']=='Ready';assert struct.unpack('<4I',file(key).read_bytes()[:16])==(0x45505056,4,480,320);checks.append('Model-only thumbnail remains 480x320')
 (folder/'preview-cache-check.json').write_text(json.dumps(dict(passed=True,checks=checks,note='Private isolated renderer at Login, not a gameplay/NavMesh check'),indent=2));print(json.dumps(checks),flush=True)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);a.add_argument('build',type=Path);x=a.parse_args();run(x.folder.resolve(),x.build.resolve())
