"""Execute the three native Crafting actions on a pinned disposable save only.

Requires player-ui-test. Uses the actual UI callbacks, confirmation, serialized
command queue and backend. No mouse/keyboard automation or original-save edits.
"""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from tools import ingame_live_session as session, le_session as le
from tools.ingame_capture import capture


class Check:
    def __init__(self, folder):
        self.folder=folder
        self.state=json.loads((folder/'session.json').read_text())
        self.process=self.state['process']
        self.id=self.state['cloneId']
        self.transcript=[]

    def req(self, command, okay=True):
        if le.game_pids()!=[self.state['pid']] or le.process_identity(self.state['pid'])!=self.process:
            raise session.UnsafeTestState('Owned process changed; no command sent.')
        value=json.loads(le.send(command,timeout=15,expected_process=self.process))
        self.transcript.append({'command':command,'response':value})
        (self.folder/'actions-transcript.json').write_text(json.dumps(self.transcript,indent=2),encoding='utf-8')
        if value.get('ok') is not okay:raise RuntimeError(f'{command}: {value.get("error")}')
        return value

    def ready(self, predicate=lambda r:True, seconds=120):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            r=self.req('panel read')
            if r.get('faults') or r.get('broken'):raise RuntimeError(r.get('error') or 'Native UI fault')
            if r['built'] and not r['jobs'] and not any(s['pendingWidgets'] for s in r['sectionList'] if s['page']==r['page'] and not s['hidden']) and predicate(r):return r
            time.sleep(.25)
        raise RuntimeError('UI did not reach the expected state.')

    def action(self, name):
        opened=self.req('panel qa:click:'+name)
        assert opened['dialog'], 'Action must require confirmation'
        # A different action cannot consume this confirmation.
        other='craft_custom' if name!='craft_custom' else 'craft_materials'
        self.req('panel qa:confirm:'+other,okay=False)
        capture(self.req,self.state['game'],self.folder/(name+'-confirmation.png'))
        self.req('panel qa:confirm:'+name)
        result=self.ready(lambda r:bool(r.get('qa',{}).get('reply')))['qa']
        value=json.loads(result['reply'])
        if not value.get('ok'):raise RuntimeError(result)
        backup=Path(value.get('backup',''))
        assert backup.is_dir() and any(backup.iterdir()), 'Successful save action must retain recovery files'
        print(json.dumps({'passed':name,'command':result['command'],'backup':str(backup)}),flush=True)
        return value

    def stock(self):
        return {(x['type'],x['id']):x['count'] for x in self.req('craftmaterialsread')['materials']}


def run(folder,loader):
    result={'saveActionsExecuted':True,'inputAutomation':False,'checks':[]}
    try:
        session.start(folder,loader,test_name='EpCraftTest',save_tests=True)
        c=Check(folder)
        identity=c.req('identityread')
        assert identity['offline'] and identity['player']['name']=='EpCraftTest' and str(identity['player']['id'])==c.id
        c.req('craftreset');c.req('panel 1');c.req('panel page:crafting');c.req('panel tab:materials')
        c.ready(lambda r:r['cached'].get('craftmaterialsread',0)>0)
        before=c.stock()
        c.req('panel qa:field:craft_materials-value=100')
        stocked=c.action('craft_materials');after=c.stock()
        assert before.keys()==after.keys() and after and all(after[k]==max(before[k],100) for k in before)
        assert all(stocked['kinds'][k]>0 for k in ('shards','runes','glyphs'))
        c.req('panel qa:field:craft_materials-value=10')
        repeated=c.action('craft_materials')
        assert repeated['updatedKinds']==0 and c.stock()==after, 'Lower targets must not reduce existing stock'
        result['checks'].append({'name':'Stock all materials','materialTypes':len(after),'higherQuantitiesPreserved':True,'backup':stocked['backup']})
        (folder/'actions-checks.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        c.req(f'crafttest {c.id} clear');fixture=c.req(f'crafttest {c.id} setup')
        c.req('panel tab:forge');c.req('panel qa:details:advanced');c.ready()
        # A fresh read is delivered through the ordinary refresh scheduling.
        time.sleep(1.5)
        preview=c.req('craftread')['preview'];selected=fixture['selectedAffix']
        forged=c.action('craft_forge')['preview']
        old={x['id']:x['tier'] for x in preview['affixes']};new={x['id']:x['tier'] for x in forged['affixes']}
        assert new[selected]>old[selected] and forged['shardsAvailable']==preview['shardsAvailable']-1
        assert forged['forgingPotential']<preview['forgingPotential']
        result['checks'].append({'name':'Craft once','affix':selected,'tierBefore':old[selected],'tierAfter':new[selected],'exactlyOneShardConsumed':True})
        (folder/'actions-checks.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        c.req(f'crafttest {c.id} clear');c.req(f'crafttest {c.id} customsetup')
        c.req('panel tab:custom');c.ready()
        c.req('panel qa:click:craft_custom_catalog')
        ui=c.ready(lambda r:r.get('sectionsState',{}).get('workshop',{}).get('loaded',False))
        # Choose through the real checkbox callback; no duplicate workshop model.
        chosen=next(h['name'] for h in ui['hits'] if h['name'].startswith('workshop-affix#') and h['enabled'])
        affix=int(chosen.split('#')[1]);c.req('panel qa:click:'+chosen);c.ready()
        custom=c.action('craft_custom');item=c.req(f'crafttest {c.id} custominspect')
        assert len(item['affixes'])==1 and item['affixes'][0]['id']==affix and item['affixes'][0]['tier']==7 and item['affixes'][0]['roll']==255
        assert c.req(f'crafttest {c.id} roundtrip')['roundtrip']
        result['checks'].append({'name':'Create custom item','item':item,'backup':custom['backup'],'nativeSerializationRoundTrip':True})
        result['stockBeforeReload']=[{'type':k[0],'id':k[1],'count':v} for k,v in c.stock().items()]
        (folder/'actions-checks.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        session.restart(folder)
        c=Check(folder)
        expected={(x['type'],x['id']):x['count'] for x in result['stockBeforeReload']}
        assert c.stock()==expected,'Material quantities did not survive a normal game restart'
        saved=session.saves._json(le.SAVES/f"1CHARACTERSLOT_BETA_{c.id}")
        # SaveItems serializes the forge slot into the character inventory. Verify
        # the exact generated game item bytes are present in the persisted save.
        wanted=item['bytes']
        def contains(value):
            if isinstance(value,list):return value==wanted or any(contains(v) for v in value)
            if isinstance(value,dict):return any(contains(v) for v in value.values())
            return False
        result['customItemBytesInSave']=contains(saved)
        assert result['customItemBytesInSave'],'Exact custom item bytes missing from the reloaded save'
        result['materialsSurvivedRestart']=True
        result['faults']=c.req('panel read')['faults']
        result['status']='passed'
        (folder/'actions-checks.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps({'status':'passed','checks':len(result['checks']),'materialsSurvivedRestart':True,'customItemBytesInSave':True}),flush=True)
    finally:
        if (folder/'session.json').exists():session.finish(folder)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--loader',type=Path,required=True)
    args=p.parse_args();run(args.folder.resolve(),args.loader.resolve(strict=True))
