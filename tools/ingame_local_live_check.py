"""Private renderer/local-preference tests. Requires player-ui-test and the isolated clone."""
import argparse
import json
from pathlib import Path
import shutil
import time
from tools import le_session as le
from tools.ingame_capture import capture as save_capture

def run(folder):
    state=json.loads((folder/'session.json').read_text()); process=state['process']
    if le.game_pids()!=[state['pid']] or le.process_identity(state['pid'])!=process:raise RuntimeError('Not the owned test process')
    def req(command):
        value=json.loads(le.send(command,timeout=8,expected_process=process))
        if not value.get('ok'):raise RuntimeError(f"{command}: {value.get('error')}")
        return value
    identity=req('identityread')
    if not identity.get('offline') or identity['player'].get('name')!='EpFeedbackTest' or str(identity['player'].get('id'))!=state['cloneId']:raise RuntimeError('Wrong character')
    def ready():
        time.sleep(.15);deadline=time.monotonic()+90
        while time.monotonic()<deadline:
            r=req('panel read')
            if r.get('faults'):raise RuntimeError(r.get('error'))
            if r['built'] and not r['jobs'] and not any(s['pendingWidgets'] for s in r['sectionList'] if s['page']==r['page'] and not s['hidden']):return r
            time.sleep(.1)
        raise RuntimeError('UI population timed out')
    def prefs(key):
        return json.loads(json.loads((folder/'preferences.json').read_text(encoding='utf-8'))['values'].get(key,'null'))
    def capture(name):
        save_capture(req,state['game'],folder/(name+'.png'))
    checks=[]
    req('panel 1');req('panel dismiss');req('panel page:settings');ready()
    req('panel local:profile-name=NativeQA');req('panel local:profile-save')
    profile=prefs('epochpact.profiles.live')['NativeQA'];assert profile
    req('panel local:profile-load');assert ready()['drafts']>0;req('panel local:discard');assert ready()['drafts']==0
    req('panel local:profile-delete');assert req('panel read')['dialog'];req('panel dismiss');assert 'NativeQA' in prefs('epochpact.profiles.live')
    checks.append('profile save/load/discard and delete cancellation; no Apply or save action')
    req('panel local:nav-favorite:summons');assert 'summons' in prefs('epochpact.display-preferences.v1')['favorites']
    req('panel local:nav-visible:equipment');assert 'equipment' in prefs('epochpact.display-preferences.v1')['hidden']
    req('panel local:nav-visible:equipment');assert 'equipment' not in prefs('epochpact.display-preferences.v1')['hidden']
    checks.append('navigation favorites and visibility')
    req('panel page:general');req('panel local:hide:card:xp');r=ready();assert 'card:xp' in prefs('epochpact.hidden-parts.v1')
    assert not any(h.get('id')=='xp' for h in r['hits'])
    req('panel page:settings');req('panel local:show-part:card:xp');ready();req('panel page:general')
    assert any(h.get('id')=='xp' for h in req('panel read')['hits']);checks.append('hide a setting, rebuild and restore without changing its value')
    req('panel layout:150');ready();req('panel stat:sheet:0');time.sleep(.4);req('panel statadvanced');r=req('panel read');assert r['dialogMaxScroll']>0
    req('panel dialogscroll:10000');r=req('panel read');bounds=r['dialogBounds'];apply=[h for h in r['hits'] if h['name']=='stat-apply']
    assert apply and bounds[1]<=apply[0]['y'] and apply[0]['y']+apply[0]['h']<=bounds[1]+bounds[3]+1
    capture('qa-stat-150-bottom');req('panel dismiss');req('panel layout:100');ready();checks.append('advanced stat modal scroll and reachable Apply at 150%; not pressed')
    req('panel page:crafting')
    for tab in ('forge','materials','custom'):
        req('panel tab:'+tab);ready();capture('qa-crafting-'+tab)
    checks.append('all three crafting tabs; save actions not executed')
    fixtures=[]
    for style in ('quiet','bars','split','table','options'):
        req('panel fixture:'+style);req('panel 0');time.sleep(.2);r=req('panel read')
        total=[c for c in r['hud']['damage']['cells'] if c['source']=='__total']
        assert total and total[-1]['text']=='8,146',total
        assert r['hud']['damage']['fixture'];capture('qa-hud-fixture-'+style)
        if style=='table':
            columns={c['field'] for c in r['hud']['damage']['cells'] if c['source']=='You'}
            assert columns=={'dps10','dps60','dps600','dps3600','average','combatAverage','total','hits','maxHit'},columns
        fixtures.append({'style':style,'total':total[-1]['text'],'cells':len(r['hud']['damage']['cells'])})
        req('panel 1')
    capture('qa-last-main-view')
    result={'checks':checks,'rendererFixtures':fixtures,'realCombatMeasured':False,'inputAutomation':False,'saveActionsExecuted':False,'faults':req('panel read')['faults']}
    (folder/'local-checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--loader',type=Path)
    args=p.parse_args();folder=args.folder.resolve()
    if args.loader:
        from tools import ingame_live_session as session
        try:
            session.start(folder,args.loader.resolve(strict=True));run(folder)
        finally:
            manifest=folder/'session.json'
            if manifest.exists() and json.loads(manifest.read_text()).get('phase')!='restored':session.finish(folder)
    else:run(folder)
