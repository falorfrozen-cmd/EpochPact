"""Photograph authored templates with the isolated native renderer (no save/world writes)."""
import argparse,hashlib,json,struct,time
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from tools import le_session as le

def run(folder,build):
 root=Path(__file__).resolve().parents[1];s=json.loads((folder/'session.json').read_text());p=s['process']
 assert s['phase']=='running' and s.get('artReviewOnly')
 def req(command):
  assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
  r=json.loads(le.send(command,timeout=15,expected_process=p));assert r.get('ok'),(command,r);return r
 assert req('sessionread')['state']=='Login'
 identity=json.loads(le.send('identityread',timeout=15,expected_process=p));assert not identity.get('offline')
 assert not identity.get('player')
 req('hideout testartpreview 1')
 raw=(build/'ingame/hideout-designs.json').read_bytes();assert raw==(root/'ui/hideout-designs.json').read_bytes()
 data=json.loads(raw);results=[]
 for index,d in enumerate(data['designs']):
  key='t:'+str(index);req('hideout testpreviewqueue '+key+' priority');end=time.monotonic()+45
  while time.monotonic()<end:
   r=req('hideout preview '+key)
   if r['ready']:break
   assert not any(w in r['status'].lower() for w in ('unavailable','too long','invalid','unknown')),r
   time.sleep(.12)
  assert r['ready'],r
  h=1469598103934665603
  for b in raw+key.encode()+b'26617preview-v4':h=((h^b)*1099511628211)&0xffffffffffffffff
  source=folder/'hideout-preview-cache'/f'{h}.rgba';pixels=source.read_bytes();magic,version,w,height=struct.unpack('<4I',pixels[:16])
  assert (magic,version,w,height)==(0x45505056,4,960,640) and len(pixels)==16+w*height*4
  im=Image.frombytes('RGBA',(w,height),pixels[16:]).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB')
  target=folder/(d['id']+'.png');im.save(target)
  results.append(dict(id=d['id'],label=d['label'],status=r['status'],image=str(target),sha256=hashlib.sha256(target.read_bytes()).hexdigest(),note='Actual isolated Unity render; scene-sourced boss doors and vendor omitted at Login. Not a live navigation test.'))
  print(d['label'],r['status'],flush=True)
 (folder/'template-art-review.json').write_text(json.dumps(dict(designSha256=hashlib.sha256(raw).hexdigest(),results=results),indent=2),encoding='utf-8')
 font=ImageFont.truetype('C:/Windows/Fonts/georgia.ttf',25);small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',16)
 canvas=Image.new('RGB',(1920,3*700),'#0c1613');pen=ImageDraw.Draw(canvas)
 for k,r in enumerate(results):
  x=(k%2)*960;y=(k//2)*700;canvas.paste(Image.open(r['image']),(x,y+40));pen.text((x+25,y+6),r['label'],font=font,fill='#e7d2a2');pen.text((x+25,y+680),'Actual Unity render - scene NPC/doors omitted - not a gameplay screenshot',font=small,fill='#9ca99f')
 canvas.save(folder/'five-designs.jpg',quality=95)
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);a.add_argument('build',type=Path);x=a.parse_args();run(x.folder.resolve(),x.build.resolve())
