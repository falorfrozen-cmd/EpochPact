"""Real local HTTP -> backend -> game check; isolated EpCraftTest only."""
import json
import time
from pathlib import Path
from urllib.request import Request, urlopen
from . import le_session as le


def verify():
    output=Path(__file__).resolve().parents[1]/'research/live/ce-features-20261009/http-live.json'
    identity=json.loads(le.send('identityread',timeout=10))['player']
    assert identity['name']=='EpCraftTest',identity
    save_id=identity['id'];base='http://127.0.0.1:17884';records=[]

    def request(path,payload=None,token=None):
        headers={'Content-Type':'application/json','Origin':base}
        if token:headers['X-Epoch-Token']=token
        with urlopen(Request(base+path,data=json.dumps(payload).encode() if payload else None,headers=headers),timeout=15) as r:
            return json.load(r)

    boot=request('/api/bootstrap');assert not boot['preview'];token=boot['token']

    def job(cid,args=None,operation='set'):
        started=time.perf_counter()
        j=request('/api/jobs',{'type':'control','id':cid,'args':args or {},'operation':operation},token)
        deadline=time.monotonic()+30
        while True:
            r=request('/api/jobs/'+j['job'])
            if r['done']:break
            if time.monotonic()>deadline:raise TimeoutError('Accepted job timed out; never resubmitted.')
            time.sleep(.1)
        records.append({'id':cid,'result':r['result'],'elapsedMs':round((time.perf_counter()-started)*1000,2)})
        output.write_text(json.dumps(records,indent=2),encoding='utf-8')
        assert r['result']['ok'],r
        print(cid,'OK',flush=True);return r['result']

    def probe(action):
        r=json.loads(le.send(f'crafttest {save_id} {action}',timeout=20));assert r['ok'],r;return r

    try:
        probe('clear');probe('setup');probe('modifierrune')
        job('craft_runes',{'value':1});before=probe('materials')['runeCount']
        preview=job('craft_read',operation='read')['forgePreview']
        assert preview['canForge'] and preview['player']['id']==save_id
        forged=job('craft_forge',{'expected_id':save_id})
        assert forged['backup'] and 'forgePreview' in forged
        assert probe('materials')['runeCount']==before
        job('map_reveal',{'value':1});assert job('map_read',operation='read')['revealed']
        job('map_reveal',{'value':0});job('craft_reset')
        print('Real HTTP / game Forge and map controls passed',flush=True)
    finally:
        probe('clear')
        le.send('mapreveal 0',timeout=10);le.send('craftreset',timeout=10)


if __name__=='__main__':verify()
