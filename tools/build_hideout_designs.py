"""Author five hideouts from exact catalog names and export reproducible placement tables.

Coordinates are local game units. Clearance is a conservative planning envelope,
not a claim about unmeasured prefab colliders; live NavMesh checks are separate.
"""
from __future__ import annotations
import csv
import json
import math
from collections import Counter
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SHAPES={'round':0,'square':1,'hall':2,'cross':3,'islands':4}
PICKS = {
    'CouncilChamberFloor_Eterra_01': ('Floor', 'Gold-and-blue sun mosaic; a formal garden centre'),
    'KV_Floor_01_I': ('Floor', 'Violet marble paving with original UVs'),
    'KV_Floor_02_I': ('Floor', 'Matching violet marble variant'),
    'KV_Small_Circle_Floor_01': ('Floor', 'Octagonal marble medallion and portal inset'),
    'KV_Floor_Circle_01': ('Floor', 'Large violet rotunda floor with worn edges'),
    'Floor_01_I_Ang_18': ('Floor', 'Carved pink ornamental floor'),
    'Floor_Stone_Circle': ('Floor', 'Subdued cobblestone court foundation'),
    'LagonFloor_Walkway': ('Floor', 'Blue-and-gold marine paving'),
    'Circle_Statue': ('Floor', 'Low concentric plinth for a portal court'),
    'Circle_Fountain': ('Monument', 'Real stepped fountain bowl; water effects not assumed'),
    'KV_Statue_01a': ('Monument', 'Violet guardian holding a gold orb'),
    'SM_Statue_01a': ('Monument', 'Small gold-and-black obelisk'),
    'Statue_Heroth_01': ('Monument', 'Dark sculptural alternate landmark'),
    'Astrolabe': ('Monument', 'Brass-and-teal orrery; intentionally enlarged'),
    'KV_Pillar_Small_Single_Cap_01': ('Architecture', 'Slender violet colonnade matching the marble court'),
    'KV_Pillar_Big_Bottom': ('Architecture', 'Large violet column for the rear skyline'),
    'KV_Pillar_Big_Base': ('Architecture', 'Matching round column base'),
    'KV_Wall_Ark_Long': ('Architecture', 'Violet double-arched rear wall'),
    'KV_Wall_Circle_Third': ('Architecture', 'Curved triple-arch gallery'),
    'KV_Wall_Circle_Third_Inner': ('Architecture', 'Vaulted apse enclosing a rear alcove'),
    'KV_Arc_Wall_01': ('Architecture', 'Tall carved violet wall; keep at the rear'),
    'KV_Railing_Small_Straight_01': ('Architecture', 'Low marble balustrade'),
    'KV_Railing_Small_Curve_10': ('Architecture', 'Curved low rotunda railing'),
    'KV_Railing_10_Curve_30': ('Architecture', 'Long curved balustrade alternate'),
    'Garden_Amphitheater_Big__round_Half': ('Architecture', 'Mossy half-round arched pergola'),
    'Garden_Pillar_Big': ('Architecture', 'Mossy garden colonnade'),
    'Garden_Wall_Straight_Arc_4m': ('Architecture', 'Garden stone arch alternate'),
    'H_Temple_Railings_01': ('Architecture', 'Low gold-and-stone carved border'),
    'TOE_Railings_Turn_L1': ('Architecture', 'Ornate green-and-gold corner'),
    'TOE_Railings_Turn_L2': ('Architecture', 'Opposite corner for a paired entry'),
    'Cemetery_Column': ('Architecture', 'Warm carved column for the forge gallery'),
    'LagonPillar_Thick': ('Architecture', 'Blue-and-gold rope-bound temple column'),
    'LagonPillar_Thin': ('Architecture', 'Slender matching marine column'),
    'CrystalCluster': ('Crystal', 'Lilac multi-point crystal cluster; no emission assumed'),
    'SM_Bush_010_a': ('Nature', 'Compact green planting with readable scale'),
    'Grass_Flower_PinkWhite_Clump': ('Nature', 'Small pink-white flowering clump'),
    'Fern1Standard': ('Nature', 'Small fern alternate; not a large canopy'),
    'Eterra Vase 01': ('Prop', 'Teal-and-orange ceramic accent'),
    'Eterra Vase 02': ('Prop', 'Planted teal garden bowl'),
    'Eterra Vase 03': ('Prop', 'Second planted teal bowl'),
    'Eterra Vase 04': ('Prop', 'Tall teal urn'),
    'SM_Urn_01a': ('Prop', 'Embossed squat gold urn'),
    'SM_Urn_02a': ('Prop', 'Carved slender gold urn'),
    'SM_Urn_04a': ('Prop', 'Tall slim metallic urn'),
    'SM_Torch_01a': ('Prop', 'Dark bronze brazier; flame not assumed'),
    'Client_Forge': ('Workshop', 'Textured circular forge, the workshop landmark'),
    'Coral_16_G1_BlueVSPro': ('Marine', 'Blue finger coral provides compact colour'),
    'Coral_RedFlower_01': ('Marine', 'Small warm coral rosette'),
    'SM_Book_2': ('Archive', 'Textured closed book for small table dressing'),
    'Shelf_Preset_02': ('Archive', 'Narrow textured ceramic display shelf'),
}

def catalog():
    with (ROOT/'ui/hideout-catalog.tsv').open(encoding='utf-8') as f:
        rows=[r for r in csv.reader(f,delimiter='\t') if r and not r[0].startswith('#')]
    return {r[3]:{'bundle':int(r[0]),'guid':r[1],'category':r[2],'index':i} for i,r in enumerate(rows)}

def box(x,y,hx,hy,corner):
    qx,qy=abs(x)-hx+corner,abs(y)-hy+corner
    return corner-math.hypot(max(qx,0),max(qy,0))-min(max(qx,qy),0)

def outline(shape,s,x,y):
    if shape=='round':return s-math.hypot(x,y)
    if shape=='square':return box(x,y,.9*s,.9*s,.12*s)
    if shape=='hall':return box(x,y,1.45*s,.62*s,.12*s)
    if shape=='cross':return max(box(x,y,1.1*s,.42*s,3),box(x,y,.42*s,1.1*s,3))
    return max(.72*s-math.hypot(x,y),.48*s-math.hypot(x-1.35*s,y),.48*s-math.hypot(x+1.35*s,y),box(x,y,1.2*s,3,1))

def row(name,x,y,span=4,yaw=0,size=1,lift=0,note='',walk=False):
    return dict(name=name,right=x,up=y,lift=lift,yaw=yaw,size=size,maxSpan=span,note=note,walkable=walk)

def stations(spots,exit):
    return [row('Stash',*spots[0],span=0,note='Functional stash'),row('Annya the Blacksmith',*spots[1],span=0,note='Functional vendor'),
            row('Cache_of_Eternity_01',*spots[2],span=0,size=.3,note='Functional Eternity Cache; explicit scale 0.3'),
            row('DOOR_RETURN',*exit,span=0,yaw=60,note='Return to the Monolith Hub; protected exit')]

def doors(spots):
    return [row(f'DOOR_{i+1}',x,y,span=0,yaw=60,note='Boss '+str(i+1) if i<10 else 'Difficulty selector' if i==10 else 'Boss Rush') for i,(x,y) in enumerate(spots)]

def pairs(name,points,span=4,yaw=0,walk=False):
    return [row(name,side*x,y,span=span,yaw=yaw if side==1 else -yaw,walk=walk) for x,y in points for side in (-1,1)]

def designs():
    from tools.hideout_layouts import create
    return create(row,stations,doors,pairs)


def generate():
    cat=catalog(); missing=(set(PICKS)|{'Stash','Cache_of_Eternity_01'})-cat.keys()
    if missing:raise ValueError('Names absent from supplied catalog: '+str(sorted(missing)))
    result={'schema':1,'catalogCount':len(cat),'models':[dict(name=n,group=g,reason=r,visualStatus='Static asset inspected with Last Epoch 1.5.4 isolated renderer; not a live navigation test',**cat[n]) for n,(g,r) in PICKS.items()], 'designs':designs()}
    measured_path=ROOT/'ui/hideout-model-metrics.json'
    measured={m['name']:m for m in json.loads(measured_path.read_text(encoding='utf-8'))['models']} if measured_path.exists() else {}
    dest=ROOT/'docs/hideout-designs';dest.mkdir(exist_ok=True)
    for design in result['designs']:
        if not 25<=len(design['items'])<=60:raise ValueError('Object count: '+design['id'])
        for item in design['items']:
            name=item['name']
            if name.startswith('DOOR_'):
                suffix=name[5:];item.update(kind=2,door=-3 if suffix=='RETURN' else int(suffix) if int(suffix)<=10 else 0 if int(suffix)==11 else -1,guid='')
            elif name=='Annya the Blacksmith':item.update(kind=0,door=-5,guid='')
            else:
                if name not in cat:raise ValueError(name)
                item.update(kind=1,door=-4 if name=='Stash' else -6 if name=='Cache_of_Eternity_01' else -100,guid=cat[name]['guid'])
            margin=outline(design['shape'],design['size'],item['right'],item['up'])
            item['nominalEdgeClearance']=round(margin,3)
            if margin<1.5:raise ValueError((design['id'],name,item['right'],item['up'],margin))
            # A model capped along each 3-D axis has a worst-case horizontal
            # corner radius span/sqrt(2), regardless of its rotation. Account
            # for the full uneven-edge amplitude, not only the anchor point.
            if item['maxSpan']:
                if name in measured and item['door']==-100:
                    raw=max(measured[name][axis] for axis in ('width','height','depth'))
                    footprint=math.hypot(measured[name]['width'],measured[name]['depth'])/2
                    item['size']=round(min(item['maxSpan']*.98/raw,(margin-1.501)/max(.0001,footprint)),6)
                    item['footprintRadius']=round(footprint*item['size'],5);item['fit']=False
                    item['note']='Measured scale and horizontal footprint; native textured model; no effects assumed'
                else:
                    item['maxSpan']=round(min(item['maxSpan'],(margin-1.5)*math.sqrt(2)),3)
                    item['footprintRadius']=round(item['maxSpan']/math.sqrt(2),5);item['fit']=True
                item['boundedFootprintEdgeClearance']=round(margin-item['footprintRadius']-1.5,3)
            if item.get('surface'):
                if name not in measured:raise ValueError('Unmeasured inlaid floor: '+name)
                item['lift']=round(item.get('surfaceTop',.04)-measured[name]['height']*item['size'],6)
            elif item['walkable']:item['lift']=.03
        points=[(i['right'],i['up']) for i in design['items'] if i['name'].startswith('DOOR_') and i['name']!='DOOR_RETURN']
        minimum=min(math.dist(a,b) for k,a in enumerate(points) for b in points[k+1:])
        design['planning']={'outsideCenters':0,'minimumDoorCenterDistance':round(minimum,3),'edgeNoiseAllowance':1.5,'minimumNominalCenterEdgeClearance':min(i['nominalEdgeClearance'] for i in design['items']),'colliderClearance':'Requires live renderer/collider and NavMesh measurements','bridgeWidth':6 if design['shape']=='islands' else None}
        with (dest/(design['id']+'.tsv')).open('w',encoding='utf-8',newline='') as f:
            w=csv.writer(f,delimiter='\t',lineterminator='\n');w.writerow(['ad','right','up','lift','yaw','size','not'])
            for item in design['items']:w.writerow([item[k] for k in ('name','right','up','lift','yaw','size')]+[item['note'] or ('Walkable floor decoration' if item['walkable'] else 'Static mesh; bounded to '+str(item['maxSpan'])+' units; visual shape unverified')])
    path=ROOT/'ui/hideout-designs.json';path.write_text(json.dumps(result,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    print(json.dumps({'catalog':len(cat),'curatedModels':len(PICKS),'designs':[{k:d[k] for k in ('id','shape','size','planning')}|{'objects':len(d['items'])} for d in result['designs']]},indent=2))
    return result

if __name__=='__main__':generate()
