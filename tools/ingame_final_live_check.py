"""Run the production in-game audit and always restore the owned clone session.

No input injection, combat simulation or save-writing UI action is used.
"""
import argparse
import json
from pathlib import Path
import time
from tools import ingame_live_session as session
from tools import ingame_extended_live_check as extended
from tools import ingame_page_live_check as browsers
from tools import le_session as le


def run(folder, loader):
    try:
        session.start(folder, loader, hud_running=True)
        state=json.loads((folder/'session.json').read_text())
        process=state['process']
        def req(command):
            reply=json.loads(le.send(command,timeout=8,expected_process=process))
            if not reply.get('ok'):raise RuntimeError(f'{command}: {reply.get("error")}')
            return reply
        deadline=time.monotonic()+20
        while time.monotonic()<deadline:
            recording=req('damagesession')
            if recording.get('active'):break
            time.sleep(.5)
        else:raise RuntimeError('Remembered recording did not resume after character readiness')
        extended.run(folder)
        browsers.run(folder)
        extra={'inputAutomation':False,'saveActionsExecuted':False,'rememberedRecordingResumed':recording.get('active')}
        extra['kerning']=req('panel kern')
        req('panel dismiss');req('panel page:crafting')
        extra['craftingTabs']=[]
        for tab in ('forge','materials','custom'):
            req('panel tab:'+tab)
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                r=req('panel read')
                if r.get('faults'):raise RuntimeError(r.get('error'))
                if not r.get('jobs') and not any(s.get('pendingWidgets') for s in r['sectionList'] if s['page']==r['page'] and not s['hidden']):break
                time.sleep(.1)
            else:raise RuntimeError('Crafting tab population timed out')
            extra['craftingTabs'].append(tab)
        extra['privateCommandsRejected']=[]
        for command in ('panel fixture:bars','panel local:profile-save'):
            reply=json.loads(le.send(command,timeout=8,expected_process=process))
            if reply.get('ok'):raise RuntimeError('Private test command present in production: '+command)
            extra['privateCommandsRejected'].append(command)
        req('panel page:settings');req('panel 0');time.sleep(3)
        extra['timing']=req('panel read')['timing']
        req('panel 1');time.sleep(3);extra['timingOpen']=req('panel read')['timing']
        extra['measurement']=req('panel measure')
        extra['identity']=req('identityread')
        if extra['identity']['player'].get('name')!='EpFeedbackTest':raise RuntimeError('Unexpected character')
        if extra['measurement'].get('drift'):raise RuntimeError('Unexpected text measurement drift')
        req('damagesession stop')
        (folder/'final-checks.json').write_text(json.dumps(extra,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'finalAudit':'passed','privateCommandsRejected':2,'craftingTabs':3,'inputAutomation':False}),flush=True)
    finally:
        manifest=folder/'session.json'
        if manifest.exists() and json.loads(manifest.read_text()).get('phase')!='restored':session.finish(folder)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--loader',type=Path,required=True)
    args=p.parse_args();run(args.folder.resolve(),args.loader.resolve(strict=True))
