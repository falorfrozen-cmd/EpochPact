"""Record real loaded layouts, paths and thumbnail timings on the owned clone."""
import argparse,json,time
from pathlib import Path
from tools import le_session as le
from tools.ingame_capture import capture
from tools.hideout_editor_live_check import navigation_width


def run(folder):
    state=json.loads((folder/'session.json').read_text());p=state['process']
    assert le.game_pids()==[state['pid']] and le.process_identity(state['pid'])==p
    def req(c):
        r=json.loads(le.send(c,timeout=12,expected_process=p));assert r.get('ok'),(c,r);return r
    def guard():
        r=req('identityread');assert r['offline'] and r['player']['name']=='EpFeedbackTest' and str(r['player']['id'])==state['cloneId']
    def wait(fn):
        end=time.monotonic()+90
        while time.monotonic()<end:
            r=fn()
            if r:return r
            time.sleep(.15)
        raise RuntimeError('Native scene/preview timed out')
    def loaded():
        r=req('hideout read');return r if r['hall'] and not any(i['waiting'] for i in r['items']) else None
    def shot(name):capture(lambda c:req('reportquiet' if c=='reportnow' else c),state['game'],folder/(name+'.png'))
    guard();req('panel 1');req('panel page:hideout');req('panel qa:click:hideout-go');wait(lambda:req('hideout read')['inside']);wait(loaded)
    data=json.loads((Path(__file__).resolve().parents[1]/'ui/hideout-designs.json').read_text(encoding='utf-8'))
    results=[];req('panel 0');req('buildset zoom 2')
    for index,d in enumerate(data['designs']):
        guard();req('hideout template ready '+str(index));wait(loaded);req('hideout go');time.sleep(1)
        audit=req('hideout audit');result={'id':d['id'],'audit':audit};results.append(result)
        result['navigation']=navigation_width(req,d);shot(d['id']+'-world')
        req('panel 1');req('panel page:general');start=time.monotonic();req('hideout preview t:'+str(index))
        wait(lambda:req('hideout preview t:'+str(index))['ready']);result['previewSeconds']=round(time.monotonic()-start,3)
        req('panel 0');(folder/'redesign-results.json').write_text(json.dumps(results,indent=2));print(d['id'],{k:v for k,v in audit.items() if k!='items'},'preview seconds',result['previewSeconds'],flush=True)
    req('buildset zoom 1')
    assert all(not any(d['audit'][k] for k in ('failed','pending','remainingScripts','unreachableStationsOrDoors','visualBoundsOutside')) for d in results)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);run(parser.parse_args().folder.resolve())
