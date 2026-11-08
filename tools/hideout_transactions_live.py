"""Exercise placement transactions in the manifest-owned disposable game only."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from tools import le_session as le


def run(folder):
    state=json.loads((folder/'session.json').read_text());process=state['process']
    assert state['phase']=='running' and le.game_pids()==[state['pid']] and le.process_identity(state['pid'])==process
    def req(command,ok=True):
        r=json.loads(le.send(command,timeout=12,expected_process=process))
        assert bool(r.get('ok'))==ok,(command,r)
        return r
    identity=req('identityread')
    assert identity['offline'] and identity['player']['name']=='EpFeedbackTest' and str(identity['player']['id'])==state['cloneId']
    def settle():
        end=time.monotonic()+60
        while time.monotonic()<end:
            r=req('hideout read')
            if r['hall'] and r['inside'] and not any(i['waiting'] for i in r['items']):return r
            time.sleep(.1)
        raise RuntimeError('Objects did not settle')
    def digest():return hashlib.sha256((folder/'hideout-test.txt').read_bytes()).hexdigest()
    def item(ident):return next(i for i in req('hideout read')['items'] if i['id']==ident)
    req('panel 0');req('hideout edit 0');settle();before=digest();original=req('hideout read')
    target=next(i for i in original['items'] if i['name']=='Stash');ident=target['id']
    req('hideout hold '+str(ident));req('hideout testpoint 3 2');req('hideout turn '+str(ident)+' 45')
    assert (item(ident)['right'],item(ident)['up'],item(ident)['yaw'])==(target['right'],target['up'],target['yaw'])
    assert digest()==before and req('hideout placementread')['ghost']
    req('hideout testpoint 999 999');assert not req('hideout placementread')['valid'];req('hideout drop',False)
    assert digest()==before
    req('hideout testpoint 3 2');req('hideout drop');assert item(ident)['right']==3 and item(ident)['up']==2
    assert item(ident)['yaw']==target['yaw']+45 and digest()!=before
    req('hideout cancel');settle();assert digest()==before
    model=req('hideout search all Eterra Vase 01')['rows'][0]['i']
    added=req('hideout add '+str(model)+' 4 1')['id'];settle();req('hideout testpoint 4 1')
    assert digest()==before and len(req('hideout read')['items'])==len(original['items'])+1
    req('hideout unhold');assert digest()==before and all(i['id']!=added for i in req('hideout read')['items'])
    added=req('hideout add '+str(model)+' 4 1')['id'];settle();req('hideout testpoint 4 1');req('hideout drop')
    assert digest()!=before and item(added)['right']==4
    req('hideout cancel');settle();assert digest()==before
    result={'passed':True,'checks':['Original transform/file unchanged while staging','Invalid ground refuses drop','Drop persists exact transform and rotation','Whole-edit cancel restores exact bytes','Pending added model is excluded from disk while loading','Cancel-object removes only pending addition','New addition persists only after valid drop'],'beforeAndRestoredSha256':before}
    (folder/'placement-transactions.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);run(p.parse_args().folder.resolve())
