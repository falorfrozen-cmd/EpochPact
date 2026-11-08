"""Main-thread hideout checks in the explicitly owned disposable session.

No desktop input. No character-save actions. Close/restore via
tools.ingame_live_session finish afterwards, including after a failed check.
"""
import argparse
import hashlib
import json
import time
import heapq
import math
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture

ROOT=Path(__file__).resolve().parents[1]


def navigation_width(req,design):
    import numpy as np
    from scipy.ndimage import distance_transform_edt
    grid=req('hideout auditgrid 0 8');rows=grid['rows'];side=grid['side'];step=grid['step'];origin=grid['origin']
    while len(rows)<side:rows+=req('hideout auditgrid '+str(len(rows))+' 8')['rows']
    free=np.array([[c=='#' for c in row] for row in rows],dtype=bool)
    distance=np.maximum(0,distance_transform_edt(free)*step-step*math.sqrt(2)/2)
    centre=round(-origin/step);start=(centre,centre)
    quality={start:float(distance[start])};queue=[(-quality[start],start)]
    while queue:
        neg,p=heapq.heappop(queue);value=-neg
        if value<quality[p]:continue
        for dx,dy in ((0,1),(1,0),(0,-1),(-1,0)):
            n=p[0]+dy,p[1]+dx
            if not (0<=n[0]<side and 0<=n[1]<side and free[n]):continue
            v=min(value,float(distance[n]))
            if v>quality.get(n,-1):quality[n]=v;heapq.heappush(queue,(-v,n))
    routes=[]
    for i in design['items']:
        if i['door']==-100:continue
        options=[v for (y,x),v in quality.items() if math.hypot(origin+x*step-i['right'],origin+y*step-i['up'])<=2]
        routes.append({'name':i['name'],'sampledNavigableCentreWidth':round(max(options,default=0)*2,2)})
    return {'step':step,'origin':origin,'rows':rows,'routes':routes,'minimumSampledNavigableCentreWidth':min(r['sampledNavigableCentreWidth'] for r in routes),'interpretation':'Baked NavMesh clearance for player centres, already reduced by the game agent footprint; 0.75-unit raster with conservative half-diagonal allowance. Not an exact physical wall-to-wall width.'}


def run(folder):
    state=json.loads((folder/'session.json').read_text());process=state['process']
    if le.game_pids()!=[state['pid']] or le.process_identity(state['pid'])!=process:raise RuntimeError('Not the owned test process')
    result={'inputAutomation':False,'characterSaveActions':False,'previews':[],'designs':[],'checks':[]}
    def save(): (folder/'hideout-results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    def req(command,allow_error=False):
        if command=='reportnow':command='reportquiet'
        value=json.loads(le.send(command,timeout=12,expected_process=process))
        if not allow_error and not value.get('ok'):raise RuntimeError((command,value))
        return value
    def guard():
        identity=req('identityread')
        if not identity.get('offline') or identity['player']['name']!='EpFeedbackTest' or str(identity['player']['id'])!=state['cloneId']:raise RuntimeError('Unexpected character: no mutation sent')
    def until(fn,seconds=60):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=fn()
            if value:return value
            time.sleep(.2)
        raise RuntimeError('Timed out waiting for the native hideout')
    def shot(name):capture(req,state['game'],folder/(name+'.png'))
    guard();req('bossdoors 1');req('qoltest '+state['cloneId']+' visit-MonolithHub:1',allow_error=True)
    until(lambda:(lambda r:r if r.get('offline') and r.get('player',{}).get('scene')=='MonolithHub' else None)(req('identityread')),90)
    until(lambda:req('hideout read').get('hall'),90);req('hideout go');until(lambda:req('hideout read').get('inside'))
    req('qoltest '+state['cloneId']+' panels-close');req('panel 1');req('panel page:hideout');time.sleep(1)
    # Pagination is exhaustive; a formerly inaccessible entry after the first 40
    # must retain its original catalog index when sorted or filtered.
    first=req('hideout browse all az 0');last=req('hideout browse all az 99999')
    assert first['total']==6134 and len(first['rows'])==12 and last['page']==last['pages']-1
    reverse=req('hideout browse all za 0');assert reverse['rows'][0]['i']==last['rows'][-1]['i']
    result['checks'].append('All 6134 models reachable through 512 catalog pages, stable indices and reverse sorting')
    # Loading a visual preview must not change the persistent layout bytes.
    layout=folder/'hideout-test.txt'
    until(lambda:layout.is_file() and not any(i['waiting'] for i in req('hideout read')['items']),90)
    time.sleep(.5);before=hashlib.sha256(layout.read_bytes()).hexdigest()
    data=json.loads((ROOT/'ui/hideout-designs.json').read_text(encoding='utf-8'))
    for model in data['models']:
        key='a:'+str(model['index']);req('hideout preview '+key)
        status=until(lambda:(lambda r:r if r['status'] not in ('Queued','Loading model','Paused','Hover to preview') else None)(req('hideout preview '+key)),60)
        result['previews'].append({'name':model['name'],'key':key,**status});save()
        print('preview',model['name'],status['status'],flush=True)
    bounds=req('hideout preview bounds');(folder/'model-bounds.json').write_text(json.dumps(bounds,indent=2),encoding='utf-8')
    assert before==hashlib.sha256(layout.read_bytes()).hexdigest()
    result['checks'].append('Asset previews leave the layout file unchanged')
    # Actual composed previews, before applying any design.
    for index,d in enumerate(data['designs']):
        key='t:'+str(index);req('hideout preview '+key)
        status=until(lambda:(lambda r:r if r['status'] not in ('Queued','Loading model','Paused','Hover to preview') else None)(req('hideout preview '+key)),90)
        result['checks'].append(d['id']+' preview: '+status['status'])
        req('panel qa:click:hideout-preview-'+key);time.sleep(.25);shot(d['id']+'-preview');save()
    assert before==hashlib.sha256(layout.read_bytes()).hexdigest()
    result['checks'].append('Template previews leave the layout file unchanged')
    req('panel dismiss');req('panel 0');req('buildset zoom 2')
    for index,d in enumerate(data['designs']):
        guard();req('hideout template ready '+str(index))
        def loaded():
            r=req('hideout read')
            if r['hall'] and not any(i['waiting'] for i in r['items']) and len(r['items'])==len(d['items']):return r
        until(loaded,90);time.sleep(1);req('hideout go');time.sleep(.4)
        audit=req('hideout audit');result['designs'].append({'id':d['id'],'audit':audit});save()
        result['designs'][-1]['navigation']=navigation_width(req,d);save()
        shot(d['id']+'-game');print('design',d['id'],{k:v for k,v in audit.items() if k!='items'},flush=True)
    # An actual library UI query, including generation-bound search and hover.
    req('panel 1');req('panel page:hideout');req('panel qa:click:hideout-tab-library')
    req('panel qa:field:hideout-search=Crystal');time.sleep(.5)
    page=req('panel read');assert not page['faults'];shot('library-search')
    choices=[h['name'] for h in page['hits'] if h.get('name','').startswith('hideout-preview-a:')]
    assert choices;req('panel qa:click:'+choices[0]);time.sleep(1);shot('library-inspect')
    # Layout operations are confined to the local test file.
    req('panel 0');guard();req('hideout edit 1');old=req('hideout read')
    model=data['models'][0];added=req('hideout add '+str(model['index'])+' 0 0');item=added['id'];time.sleep(1)
    req('hideout at '+str(item)+' 3 0');req('hideout turn '+str(item)+' 45');req('hideout size '+str(item)+' 0.8');req('hideout lift '+str(item)+' 0.25')
    rejected=req('hideout at '+str(item)+' 999 999',allow_error=True);assert not rejected['ok']
    bad=req('hideout turn '+str(item)+' nan',allow_error=True);assert not bad['ok']
    req('hideout remove '+str(item));req('hideout cancel');time.sleep(1)
    restored=req('hideout read');assert len(restored['items'])==len(old['items'])
    out=next(i for i in restored['items'] if i['door']==-3);assert not req('hideout remove '+str(out['id']),allow_error=True)['ok']
    result['checks'].append('Add, move, rotate, resize, lift, remove, cancel; invalid coordinates and NaN rejected; exit protected')
    req('hideout edit 1');shot('editing-toolbar');req('hideout edit 0');req('buildset zoom 1')
    result['panelFaults']=req('panel read')['faults'];save()
    failures=[d['id'] for d in result['designs'] if any(d['audit'][k] for k in ('failed','pending','remainingScripts','unreachableStationsOrDoors','visualBoundsOutside'))]
    if failures:raise RuntimeError('Live design failures: '+', '.join(failures))
    print('Hideout live checks passed',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
