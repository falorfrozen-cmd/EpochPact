"""Readable authored layouts and conservative route planning (not game screenshots)."""
import heapq
import json
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from tools import build_hideout_designs as author


def paths(d):
    extent=math.ceil((1.83 if d['shape']=='islands' else 1.45 if d['shape']=='hall' else 1.1 if d['shape']=='cross' else 1)*d['size'])
    step=.75;half=math.ceil(extent/step)
    obstacles=[(i['right'],i['up'],i.get('footprintRadius',i['maxSpan']/math.sqrt(2))) for i in d['items'] if i['maxSpan'] and not i['walkable']]
    free={}
    for gx in range(-half,half+1):
        for gy in range(-half,half+1):
            x,y=gx*step,gy*step
            c=author.outline(d['shape'],d['size'],x,y)-1.5
            if c<=0:continue
            c=min([c]+[math.hypot(x-ox,y-oy)-r for ox,oy,r in obstacles])
            if c>0:free[gx,gy]=c
    if (0,0) not in free:raise ValueError('Blocked spawn: '+d['id'])
    quality={(0,0):free[0,0]};previous={};queue=[(-quality[0,0],(0,0))]
    while queue:
        neg,p=heapq.heappop(queue);value=-neg
        if value<quality[p]:continue
        for dx,dy in ((0,1),(1,0),(0,-1),(-1,0)):
            n=p[0]+dx,p[1]+dy
            if n not in free:continue
            v=min(value,free[n])
            if v>quality.get(n,-1):quality[n]=v;previous[n]=p;heapq.heappush(queue,(-v,n))
    routes=[]
    for i in d['items']:
        if i['door']==-100:continue
        candidates=[p for p in quality if math.hypot(p[0]*step-i['right'],p[1]*step-i['up'])<=2]
        if not candidates:raise ValueError('No planned access: '+d['id']+' '+i['name'])
        end=max(candidates,key=lambda p:quality[p]);width=2*quality[end];route=[end]
        while route[-1]!=(0,0):route.append(previous[route[-1]])
        routes.append({'name':i['name'],'plannedClearWidth':round(width,2),'route':[(x*step,y*step) for x,y in reversed(route)]})
    return routes


def draw(d,routes,dest):
    w,h=1200,940;im=Image.new('RGB',(w,h),'#07110f');pen=ImageDraw.Draw(im)
    serif=ImageFont.truetype('C:/Windows/Fonts/georgia.ttf',32);small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',16)
    pen.rectangle((16,16,w-17,h-17),outline='#9f8956',width=2)
    pen.text((38,32),d['label'],font=serif,fill='#eddfb9')
    pen.text((40,78),f"{d['shape']} · S {d['size']} · {d['texture']} · {len(d['items'])} objects",font=small,fill='#a9bbb0')
    extent=1.83*d['size'] if d['shape']=='islands' else 1.45*d['size'] if d['shape']=='hall' else 1.1*d['size'] if d['shape']=='cross' else d['size']
    scale=min(1040/(2*extent),660/(2*extent));cx,cy=600,490
    xy=lambda x,y:(cx+x*scale,cy-y*scale)
    for py in range(130,832,2):
        for px in range(50,1150,2):
            x,y=(px-cx)/scale,(cy-py)/scale
            distance=author.outline(d['shape'],d['size'],x,y)
            if distance>=0:pen.rectangle((px,py,px+1,py+1),fill='#20362d' if distance>1.5 else '#6a6550')
    for r in routes:
        route=[xy(*p) for p in r['route']]
        if len(route)>1:pen.line(route,fill='#547c69',width=3)
    for i in sorted(d['items'],key=lambda i:i['walkable'],reverse=True):
        x,y=xy(i['right'],i['up']);rad=max(3,i['maxSpan']*scale/2)
        if i['name'].startswith('DOOR_'):
            pen.rounded_rectangle((x-11,y-11,x+11,y+11),3,fill='#496a71',outline='#bdd2cd',width=1)
            label=i['name'].replace('DOOR_','').replace('RETURN','EXIT');pen.text((x+13,y-10),label,font=small,fill='#e0e9df')
        elif i['door']!=-100:
            pen.ellipse((x-9,y-9,x+9,y+9),fill='#ceb16d');label={-4:'Stash',-5:'Annya',-6:'Cache'}[i['door']]
            pen.text((x+12,y-11),label,font=small,fill='#eadbb4')
        else:
            color='#3a5749' if i['walkable'] else '#8d8064'
            pen.ellipse((x-rad,y-rad,x+rad,y+rad),outline=color,width=2)
            if i['maxSpan']>=5 and not i['walkable']:pen.text((x+4,y-20),i['name'].replace('_',' '),font=small,fill='#c1bba7')
    x,y=xy(0,0);pen.polygon(((x,y-7),(x-7,y+6),(x+7,y+6)),fill='#bfe1b6');pen.text((x+10,y+4),'Arrival',font=small,fill='#bfe1b6')
    pen.text((40,847),'TOP-DOWN PLANNING DIAGRAM · NOT A GAME SCREENSHOT',font=small,fill='#d3bd85')
    pen.text((40,875),'Gold: services   Blue: gates   Circles: capped decor envelopes   Green: reserved approach routes',font=small,fill='#b3c5b8')
    pen.text((40,902),'Model appearances and native collider clearance require separate in-game verification.',font=small,fill='#84998b')
    im.save(dest)


def generate():
    root=author.ROOT;data=json.loads((root/'ui/hideout-designs.json').read_text(encoding='utf-8'));dest=root/'docs/hideout-designs'
    md=['# Hideout designs and implementation', '', 'These diagrams are plans, not game screenshots. All model names and GUIDs come from the supplied catalog. The selected assets were inspected with the isolated Last Epoch 1.5.4 renderer; their measured bounds resolve the numeric scales in the exported tables. A successful thumbnail is not a guarantee that every catalog asset has finished textures, collision or a useful static pose.', '',
        'The live system provides ten timeline bosses, Difficulty and Boss Rush (12 destinations), plus a protected return to the Hub. The design does not invent two extra boss encounters. Arrival is `(0, 0)`; the return gate is a separate object. Gameplay functionality is provided by the existing native stations, never decorative prefab scripts.', '',
        '## Curated models', '', '| Exact catalog name | Group | Reason | Appearance |', '|---|---|---|---|']
    for m in data['models']:md.append(f"| {m['name']} | {m['group']} | {m['reason']} | Static thumbnail inspected in 1.5.4; individual materials/pose remain model dependent |")
    md+=['','## Five layouts','']
    metrics=[]
    for d in data['designs']:
        try:
            routes=paths(d);width=min(r['plannedClearWidth'] for r in routes);limitation=''
        except ValueError as error:
            routes=[];width=None;limitation=str(error)
        metrics.append({'id':d['id'],'minimumPlannedApproachWidth':width,'conservativeEnvelopeLimitation':limitation,'routes':routes})
        draw(d,routes,dest/(d['id']+'.png'))
        md += [f"### {d['label']}",'',d['description'],'',f"Ground: **{d['shape']}, {d['size']}, {d['texture']}**. Arrival `(0, 0)`. Services form a short local shopping route; larger scenery occupies the north or outside perimeter. The empty areas are approach lanes and space for the player's own additions, rather than missing decoration.",'',
            f"![Planning diagram]({d['id']}.png)",'',f"Placements: [{d['id']}.tsv]({d['id']}.tsv). Numeric scales use the actual measured renderer bounds. Unmeasured model entries retain runtime span caps; the Cache retains its required 0.3 scale.",'',
            f"Checks: **0 outside anchors**, minimum gate centre separation **{d['planning']['minimumDoorCenterDistance']} units**. Decorative span envelopes include a 1.5-unit uneven-edge allowance. Conservative envelope approach width: **{width if width is not None else "not certified"}**. {limitation} The proxy uses conservative circular renderer envelopes, including empty arch openings; it does not reproduce native colliders. Use the separate native NavMesh results for in-game accessibility; these diagrams do not certify physical clearance.",'',
            'Risks: imported pivots, unusually wide Cache geometry at its required 0.3 scale, imported architectural bounds, shader-dependent foliage, and the actual native gate collision shapes. First inspect the focal object, then each service and gate approach. Removing gates leaves the outer decorative composition intact. Effect-only assets need testing and are not the visual foundation of any layout.','']
    md += ['## Editor changes','',
        '- In-hideout button above the skill bar, with add/library, undo, cancel and done controls.',
        '- Separate Templates, Model library, Placed objects and Ground tabs.',
        '- Full catalog pagination, category search and A–Z / Z–A sorting; search replies are generation-bound and typing is debounced.',
        '- Thumbnails and enlarged hover inspection from a separate, read-only static preview scene. No template is applied just by hovering.',
        '- Scripts, animation, audio and particles are removed/stopped before decoration activation. Native functionality belongs to the station proxies.',
        '- Measured horizontal footprints and resolved scale for authored models; automatic span caps for library additions, exit protection and finite transform checks.',
        '- 400 placed-object cap and 32 preview-texture LRU cap to keep the editor bounded. Model and bundle loads are asynchronous; ready pictures survive template changes and use a versioned local disk cache. Pending assembly pauses when the panel closes.','',
        '## Runtime verification','',
        'See [dev64 verification and limitations](dev64-verification.md) for the final build, actual screenshots, native NavMesh routes, sampled clearances, UI checks and shutdown results. Those measurements are separate from the conservative planning grid above. The full catalog is searchable, but only the curated subset was exhaustively rendered. Model-only prefabs with placeholder or unsupported materials use an existing game stone material on the copy; original game assets are unchanged.','',
        '## Preferred theme','',
        '**The Moonstone Court**: continuous violet-marble paving, a guardian monument and matching vaulted architecture give it a coherent identity. Both sides have separate portal galleries and the foreground service court stays low and readable.']
    (dest/'README.md').write_text('\n'.join(md)+'\n',encoding='utf-8')
    (dest/'route-planning.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    print(json.dumps([{'id':r['id'],'plannedWidth':r['minimumPlannedApproachWidth']} for r in metrics]))


if __name__=='__main__':generate()
