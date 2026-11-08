"""Real Unity-rendered asset contact sheets for hideout art direction; no desktop input."""
import argparse,csv,json,time,struct
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from tools import le_session as le
NAMES='''Council_Chamber_Floor_01
CouncilChamberFloor_Eterra_01
Floor_01
Floor_01_I_Ang_18
Floor_Stone_4m
Floor_Stone_Circle
KV_Floor_01_I
KV_Floor_02_I
KV_Floor_Circle_01
KV_Small_Circle_Floor_01
TOE_Floor_Cap_090
TOE_Floor_Ring_090
LagonFloor_CirclePiece
LagonFloor_Walkway
BazaarFloor
WeaversFloor_Full
Circle_Fountain
Circle_Fountain_Damaged_Roses
Circle_Statue
Eterra_Statue_Model
Lagon_Statue_Model
Heorot_Statue_Model
KV_Statue_01a
SM_Statue_01a
TOE_Statue_01
Welryn_Statue_01
Arch_Big_01
Arch_Door_07
GardenRoadArch
Garden_Amphitheater_Big__round_Half
GardenGroupTop
GardenGroupBottom
Garden_Pillar_Big
Cemetery_Column
Cemetery_Column_Pillar
CATA_ModularPillar
ImpSet_Pillar_01
ImpSet_PillarBase_01
KV_Pillar_Small_Single_Cap_01
TOE_Statue_Pillar
Garden_Wall_Straight_fountain_4m
Garden_Wall_Straight_Arc_4m
H_Temple_Railings_01
ImpSet_Railing_Bar
KV_Railing_Small_Straight_01
Cemetery_WallWithTrim
WelrynGardenStripLow
GardenVegetation
Tree_002_c_BigTree
Tree_002_d_Bush
SM_Bush_010_a
Grass_Flower_PinkWhite_Clump
IvyFlowering_E
Astrolabe
ALC_Crystall_Ball
CrystalCluster
BlueCrystalVSPro
IceCrystal
ScrollShelf
Shelf_Preset_01
Shelf_Tall
Books_Preset_01
ALC_Table
sm_Table_01
sm_Carpet_01
sm_Carpet_06
Rec_Rug
Rug
fantasybench1
SM_Urn_01a
Eterra Vase 01
CATA_BrazierStanding
Lantern_02
Candles_01
SM_Furnace
SM_Furnace_Edit
SM_Bellows
FurnaceCageGroupLow
GearsWithChainLow
Anvil
Client_Forge
Nagasa_Banner_01
Banner_02_Short'''.splitlines()

def run(folder):
 root=Path(__file__).resolve().parents[1];s=json.loads((folder/'session.json').read_text());p=s['process'];assert s['phase']=='running' and 'panelBuilt' in s
 def req(c):
  assert le.game_pids()==[s['pid']] and le.process_identity(s['pid'])==p
  r=json.loads(le.send(c,timeout=15,expected_process=p));assert r.get('ok'),(c,r);return r
 r=json.loads(le.send('identityread',timeout=15,expected_process=p))
 if r.get('offline'):
  assert r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==s['cloneId'];req('bossdoors 1');req('hideout go')
 else:
  assert s.get('artReviewOnly') and req('sessionread')['state']=='Login';req('hideout testartpreview 1')
 if r.get('offline'):req('panel 1');req('panel page:general')
 rows=[r for r in csv.reader((root/'ui/hideout-catalog.tsv').open(encoding='utf-8'),delimiter='\t') if r and not r[0].startswith('#')];indices={r[3]:i for i,r in enumerate(rows)}
 raw=(root/'ui/hideout-designs.json').read_bytes();result=[]
 def cache(key):
  h=1469598103934665603
  for b in raw+key.encode()+b'26617preview-v4':h=((h^b)*1099511628211)&0xffffffffffffffff
  return folder/'hideout-preview-cache'/f'{h}.rgba'
 for name in NAMES:
  assert name in indices,name
  key='a:'+str(indices[name]);req('hideout testpreviewqueue '+key+' priority');end=time.monotonic()+28
  while time.monotonic()<end:
   r=req('hideout preview '+key)
   if r['ready'] or r['status'] not in ('Queued','Loading model','Paused','Hover to preview'):break
   time.sleep(.12)
  record=dict(name=name,index=indices[name],key=key,**r);file=cache(key)
  if r['ready'] and file.exists():record['pixels']=str(file)
  result.append(record);(folder/'asset-review.json').write_text(json.dumps(result,indent=2));print(name,r['status'],flush=True)
 bounds=req('hideout preview bounds');(folder/'review-bounds.json').write_text(json.dumps(bounds,indent=2))
 font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',15);small=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',12)
 for start in range(0,len(result),16):
  group=result[start:start+16];sheet=Image.new('RGB',(1280,((len(group)+3)//4)*250),'#101916');draw=ImageDraw.Draw(sheet)
  for k,m in enumerate(group):
   x=(k%4)*320;y=(k//4)*250
   if 'pixels' in m:
    file=Path(m['pixels']);header=struct.unpack('<4I',file.read_bytes()[:16]);assert header[2:]==(480,320)
    im=Image.frombytes('RGBA',(480,320),file.read_bytes()[16:]).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert('RGB');sheet.paste(im.resize((318,212)),(x,y))
   draw.text((x+5,y+214),f"{m['index']}  {m['name']}",font=font,fill='#ead6a4');draw.text((x+5,y+235),m['status'][:48],font=small,fill='#9eae9f')
  target=folder/f'assets-{start//16}.jpg';sheet.save(target,quality=94)
 if r.get('offline'):req('panel 0')
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('folder',type=Path);run(a.parse_args().folder.resolve())
