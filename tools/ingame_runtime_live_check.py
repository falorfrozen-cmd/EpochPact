"""Actual-resolution, combat-meter and Echo-focus checks on the disposable clone.

No desktop input injection. Screen mode is restored before the normal shutdown;
the session helper restores the exact original runtime, saves and preferences.
"""
from __future__ import annotations
import argparse
import ctypes
import json
import math
import time
import traceback
from pathlib import Path
from tools import ingame_live_session as session, le_session as le
from tools.ingame_actions_live_check import Check
from tools.ingame_capture import capture


def until(fn,seconds=90):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=fn()
        if value:return value
        time.sleep(.3)
    raise RuntimeError('Live condition timed out')


def plain(c,line):
    if le.game_pids()!=[c.state['pid']] or le.process_identity(c.state['pid'])!=c.process:
        raise session.UnsafeTestState('Owned process changed.')
    identity=c.req('identityread')
    if not identity.get('offline') or str(identity.get('player',{}).get('id'))!=c.id or identity['player'].get('name')!=c.state['cloneName']:
        raise session.UnsafeTestState('Loaded character changed; no setting or travel command sent.')
    value=le.send(line,timeout=15,expected_process=c.process) or ''
    if not value or 'refused' in value.lower():raise RuntimeError(line+': '+value)
    return value


def resolutions(c):
    original=c.req('panel qa:resolution');checks=[]
    try:
        for width,height in ((1920,1080),(1280,720),(2560,1440)):
            c.req(f'panel qa:resolution:{width}:{height}:3')
            until(lambda:(lambda r:r if (r['width'],r['height'])==(width,height) else None)(c.req('panel qa:resolution')),30)
            for size in (100,150):
                c.req('panel 0');c.req('panel 1');c.req('panel layout:'+str(size));c.ready()
                c.req('panel page:crafting');c.req('panel tab:custom');ui=c.ready()
                assert ui['screen']==[width,height] and math.isfinite(ui['scale']) and ui['scale']>0
                c.req('panel page:stats');c.ready();c.req('panel stat:sheet:0');time.sleep(.4);c.req('panel statadvanced')
                ui=c.req('panel read')
                if ui['dialogMaxScroll']:c.req('panel dialogscroll:10000');ui=c.req('panel read')
                apply=next(h for h in ui['hits'] if h['name']=='stat-apply');bounds=ui['dialogBounds']
                assert bounds[0]<=apply['x'] and apply['x']+apply['w']<=bounds[0]+bounds[2]+1
                assert bounds[1]<=apply['y'] and apply['y']+apply['h']<=bounds[1]+bounds[3]+1
                capture(c.req,c.state['game'],c.folder/f'resolution-{width}-{height}-{size}-dialog.png')
                c.req('panel dismiss');c.req('panel page:crafting');c.req('panel tab:custom');c.ready()
                capture(c.req,c.state['game'],c.folder/f'resolution-{width}-{height}-{size}-workshop.png')
                checks.append({'width':width,'height':height,'textScale':size,'reachableApply':True,'faults':ui['faults']})
                print(json.dumps({'resolutionPassed':checks[-1]}),flush=True)
    finally:
        c.req('panel dismiss');c.req('panel qa:resolution:{width}:{height}:{mode}'.format(**original))
        until(lambda:(lambda r:r if all(r[k]==original[k] for k in ('width','height','mode')) else None)(c.req('panel qa:resolution')),30)
        c.req('panel 0');c.req('panel 1');c.req('panel layout:100');c.ready()
    return {'checks':checks,'originalModeRestored':True}


def combat(c):
    q=lambda action:c.req(f'qoltest {c.id} '+action)
    if c.req('identityread')['player']['scene']!='Z52':
        q('visit-Z52')
        def entered():
            value=json.loads(le.send('identityread',timeout=8,expected_process=c.process))
            return value if value.get('ok') and value.get('player',{}).get('scene')=='Z52' else None
        until(entered,90)
    time.sleep(1)
    # Give only the disposable clone ample health; it is a session bonus.
    plain(c,'stat health 100000');q('player-heal')
    foes=q('enemies')['enemies']
    if not foes:q('near-spawner');time.sleep(2);foes=q('enemies')['enemies']
    assert foes,'No live enemies for a real combat check'
    q('near-enemy-level');q('player-heal')
    q('summon-at-379');time.sleep(1)
    allies=q('summon-where')['summons']
    assert allies,'Real summon creation failed'
    q('ally-gather-0')
    targets=q('dummy-area-65536');assert targets['targets']>0
    skills=q('skill-state')
    c.req('damagesession start')
    q('dummy-area-read') # start the independent health-loss audit at this boundary
    for index in range(min(3,len(skills['equipped']))):
        q('player-attack-'+str(index));time.sleep(.7)
    for _ in range(8):
        q('ally-aim-0');q('player-attack-0');q('player-heal');time.sleep(.5)
    time.sleep(2)
    stopped=c.req('damagesession stop');audit=q('dummy-area-read')
    rows=stopped['types'];you=next((r for r in rows if r['name']=='You'),None)
    allies_total=sum(r['total'] for r in rows if r['name']!='You')
    assert you and you['total']>0 and allies_total>0,'Both character and summon must deal real damage'
    assert abs(allies_total-stopped['allies']['total'])<=.2
    assert not audit['failed'] and audit['matched']>0,audit
    for row in audit['types']:
        assert abs(row['recorded']-row['lost'])<=max(.2,row['hits']*.02),row
        actual=next(r for r in rows if r['name']==row['name'])
        # Targets cover the nearby area. Projectiles can also hit a foe beyond it;
        # those hits still belong in the session, as the previous DPS audit documents.
        assert actual['hits']>=row['hits'] and actual['total']+.2>=row['recorded'],row
        if actual['hits']==row['hits']:assert abs(actual['total']-row['recorded'])<=max(.2,row['hits']*.02),row
    for row in rows:
        # DamageState serializes totals, seconds and rates to one decimal.
        # Compute the possible quotient interval instead of treating the rounded
        # seconds as the original timer value (e.g. 10.25 s is reported as 10.2).
        seconds=stopped['elapsedSeconds']
        low=max(0,row['total']-.05)/(seconds+.05)
        high=(row['total']+.05)/max(.001,seconds-.05)
        assert low-.051<=row['average']<=high+.051,(row['name'],'rounded session average',row['average'],low,high)
        if not stopped['unsplitHits']:assert abs(sum(row['elements']['total'])-row['total'])<=.5
    # Frozen data is stable while more native attacks happen after Stop.
    q('player-attack-0');time.sleep(.6);assert c.req('damagesession')['types']==rows
    c.req('panel page:summons');c.ready()
    ui=c.req('panel read')
    if not ui['hud']['damage']['open']:c.req('panel hud:damage')
    until(lambda:(lambda r:r if r['hud']['damage'].get('cells') else None)(c.req('panel read')))
    c.req('panel 0');time.sleep(1.5)
    wanted_icons={r['icon'] for r in rows if r.get('icon')}
    until(lambda:wanted_icons.issubset(set(c.req('panel read')['hud']['damage'].get('iconsLoaded',[]))),20)
    rendered=c.req('panel read')['hud']['damage']
    assert not rendered.get('fixture')
    assert rendered['style']=='table' and rendered['w']>=900,'Full table must expand a remembered narrow HUD width'
    for source in rows:
        cell=next(v for v in rendered['cells'] if v['source']==source['name'] and v['field']=='total')
        assert abs(float(cell['text'].replace(',',''))-source['total'])<=1
    total=next(v for v in rendered['cells'] if v['source']=='__total' and v['field']=='value')
    assert abs(float(total['text'].replace(',',''))-sum(r['dps10'] for r in rows))<=1
    capture(c.req,c.state['game'],c.folder/'real-combat-dps-hud.png')
    c.req('panel 1');c.req('damagesession reset');assert c.req('damagesession')['hits']==0
    return {'session':stopped,'healthAudit':audit,'hud':rendered,'realAbilities':True,'stopAndReset':True}


def echo(c):
    monolith=c.req('monolithread')
    if monolith['player']['scene']!='M_Rest':
        response=plain(c,'monolithrest '+c.id)
        assert 'normal waypoint transition requested for M_Rest' in response
        until(lambda:(lambda r:r if r.get('ok') and r.get('player',{}).get('scene')=='M_Rest' and r.get('restContextReady') else None)(c.req('monolithread')),90)
    monolith=c.req('monolithread')
    timeline=str((monolith.get('selected') or {}).get('timeline') or monolith['timelines'][0]['id'])
    difficulty='normal';where=f'{c.id} {timeline} {difficulty}'
    web=c.req('echoread '+where)
    if not web['generated']:
        c.req('monolithselect '+where)
        until(lambda:(lambda r:r if r.get('panelReady') else None)(c.req('monolithpanelready '+where)),90)
        web=c.req('echoread '+where)
    assert web['generated'] and web['echoes']
    c.req('panel page:monolith');c.ready()
    c.req('panel qa:click:echo_read')
    def found():
        r=c.req('panel read')
        section=next(s for s in r['sectionList'] if s['page']=='monolith' and s['kind']=='echo_results')
        c.req('panel scroll:'+str(max(0,section['y']-250)))
        r=c.req('panel read')
        matches=[h for h in r['hits'] if h['name'].startswith('echo-focus-') and h['enabled']]
        return matches[0] if matches else None
    hit=until(found,90);index=int(hit['name'].rsplit('-',1)[1])
    before=c.req('identityread')
    c.req('panel qa:click:'+hit['name'])
    done=c.ready(lambda r:r.get('qa',{}).get('command','').startswith('echofocus '))
    focused=json.loads(done['qa']['reply'])
    assert focused['ok'] and focused['selectionVerified'] and focused['focused']==index
    after=c.req('identityread');assert before['player']['scene']==after['player']['scene']=='M_Rest'
    capture(c.req,c.state['game'],c.folder/'echo-focused-without-starting.png')
    return {'webEchoes':len(web['echoes']),'focused':focused,'noEchoStarted':True}


def run(folder,loader,checks=('resolutions','realCombat','echoFocus')):
    result={'inputAutomation':False,'checks':{},'errors':{}}
    try:
        session.start(folder,loader,save_tests=True,hud_options={'style':'table','metric':'dps10','element':-1,'view':'dealt','compact':False,'bar':'always','backgroundOpacity':85})
        c=Check(folder);c.req('panel 1')
        buffer=ctypes.create_string_buffer(64);xinput=ctypes.WinDLL('xinput9_1_0')
        result['controllerHardware']={'results':[xinput.XInputGetState(i,buffer) for i in range(4)]}
        for name,fn in (('resolutions',resolutions),('realCombat',combat),('echoFocus',echo)):
            if name not in checks:continue
            try:result['checks'][name]=fn(c)
            except Exception as exc:
                result['errors'][name]=str(exc) or type(exc).__name__
                (folder/(name+'-failure.txt')).write_text(traceback.format_exc(),encoding='utf-8')
                print(json.dumps({'failed':name,'error':result['errors'][name]}),flush=True)
                if isinstance(exc,session.UnsafeTestState):raise
            (folder/'runtime-checks.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        if result['errors']:raise RuntimeError('Some live checks failed; see runtime-checks.json')
    finally:
        if (folder/'session.json').exists():session.finish(folder)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--loader',type=Path,required=True)
    p.add_argument('--checks',nargs='+',choices=('resolutions','realCombat','echoFocus'),default=('resolutions','realCombat','echoFocus'))
    args=p.parse_args();run(args.folder.resolve(),args.loader.resolve(strict=True),args.checks)
