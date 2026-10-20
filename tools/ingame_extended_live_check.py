"""All-page/read-only navigation and local layout/HUD checks in the owning clone session."""
import json
from pathlib import Path
import shutil
import time
import argparse
from tools import le_session as le
from tools.ingame_capture import capture as save_capture

def run(folder):
    state=json.loads((folder/'session.json').read_text()); process=state['process']
    if state['phase']!='running' or le.game_pids()!=[state['pid']] or le.process_identity(state['pid'])!=process:
        raise RuntimeError('Not the owned test process')
    def request(command):
        r=json.loads(le.send(command,timeout=8,expected_process=process))
        if not r.get('ok'): raise RuntimeError(f"{command}: {r.get('error')}")
        return r
    identity=request('identityread')
    if not identity.get('offline') or identity['player'].get('name')!='EpFeedbackTest' or str(identity['player'].get('id'))!=state['cloneId']:
        raise RuntimeError('Wrong character; no UI actions sent')
    results={'inputAutomation':False,'saveActions':False,'pages':[],'captures':[]}
    def ready():
        until=time.monotonic()+120
        while time.monotonic()<until:
            r=request('panel read')
            if r.get('faults') or r.get('broken'): raise RuntimeError(r.get('error') or 'UI fault')
            if r.get('built') and not r.get('jobs') and not any(s.get('pendingWidgets') for s in r['sectionList'] if s['page']==r['page'] and not s['hidden']):return r
            time.sleep(.1)
        raise RuntimeError('UI population timeout')
    def capture(name):
        save_capture(request,state['game'],folder/(name+'.png')); results['captures'].append(name)
    request('panel 1');request('panel dismiss')
    for page in ('overview','game_setup','general','loot','crafting','stats','skills','equipment','summons','display','world','inventory','atlas','stash','campaign','monolith','cof','factions','settings'):
        request('panel page:'+page);r=ready(); m=request('panel measure')
        if m.get('drift'):raise RuntimeError(f'Text drift on {page}')
        results['pages'].append({'page':page,'faults':r['faults'],'height':r['heights'][page],'measured':m})
        if page in ('game_setup','loot','summons','settings'):capture('extended-'+page)
    request('panel search:craft');r=ready()
    hits=[h for h in r['hits'] if h.get('name','').startswith('search:')]
    if not hits:raise RuntimeError('No global search results')
    results['searchResults']=len(hits);capture('extended-search');request('panel search:')
    request('panel page:settings');request('panel scroll:1800');capture('extended-settings-lower')
    for name in ('damage','feed'):
        if not request('panel read')['hud'][name]['open']:request('panel hud:'+name)
    request('panel 0');time.sleep(.5)
    assert all(request('panel read')['hud'][name]['open'] for name in ('damage','feed'))
    capture('extended-huds')
    request('panel 1');request('panel hud:damage');request('panel hud:feed')
    for scale in (150,90,100):
        request('panel layout:'+str(scale));time.sleep(.2);r=ready();request('panel page:settings');capture('extended-scale-'+str(scale))
        results.setdefault('scales',[]).append({'percent':scale,'scale':r['scale'],'faults':r['faults'],'screen':r['screen']})
    (folder/'extended-checks.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'pages':len(results['pages']),'searchResults':len(hits),'scales':len(results['scales']),'captures':len(results['captures']),'faults':0}),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
