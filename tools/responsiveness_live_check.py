"""Fresh isolated release regression; restore only this run's verified owner bytes."""
import argparse
import copy
import json
import re
import time
from pathlib import Path
from . import le_session as le, monolith_live_check as isolated, progression_backend as progression
from .ui_bridge import UiBridge

isolated.MANIFEST = le.OUT / 'responsiveness-20261008-manifest.json'
EVIDENCE = le.OUT / 'responsiveness-20261008-checks.json'


def prepare():
    manifest = isolated.prepare('EpResponsiveTest')
    # Only the NEW test file uses the historical pre-campaign snapshot. The
    # owner's freshly backed-up level-55 save and shared stash remain intact.
    old = le.GAME / 'EpochPact/backups/progression/22472-48892421/live-character.json'
    data = progression._json(old)
    assert data['characterName'] == 'Falor' and data['level'] == 8
    data.update(id=manifest['id'], characterName='EpResponsiveTest', soloChallenge=True, soloCharacterChallenge=True)
    target = le.SAVES / manifest['testFile']
    text = target.read_text(encoding='utf-8-sig')
    target.write_text(text[:text.index('{')] + json.dumps(data, separators=(',', ':')), encoding='utf-8')
    return {'id':manifest['id'], 'freshOwnerBackup':manifest['backup'], 'original':manifest['originalCharacter']}


def verify():
    manifest = json.loads(isolated.MANIFEST.read_text())
    assert not manifest['restored']
    sid = manifest['id']
    report = {'ok':False, 'releaseReady':False, 'checks':[], 'transcript':[]}
    def save():
        EVIDENCE.write_text(json.dumps(report, indent=2), encoding='utf-8')
    def command(cmd):
        start = time.perf_counter(); reply = le.send(cmd, timeout=90)
        report['transcript'].append({'command':cmd, 'elapsedMs':round((time.perf_counter()-start)*1000,2), 'reply':reply})
        save()
        assert reply and 'refused' not in reply and 'guarded failure' not in reply, (cmd,reply)
        return reply
    def check(condition, label):
        assert condition, label
        report['checks'].append(label); save(); print('PASS',label,flush=True)
    identity = json.loads(command('identityread'))
    check(identity['player']['id'] == sid and identity['player']['name']=='EpResponsiveTest', 'isolated character verified')
    baseline = command('statraw 21 0 0 0')
    base = int(re.search(r'gameValue=(-?\d+)',baseline)[1])
    check(base>0, 'actual Intelligence baseline read from CharacterStats')
    for sp in (19,20,21,22,23):
        key=f'{sp} 0 0 0'
        before=int(re.search(r'gameValue=(-?\d+)',command('statraw '+key))[1])
        after=command('statraw '+key+' increased 1')
        check(int(re.search(r'gameValue=(-?\d+)',after)[1])==before*2, f'attribute {sp}: +100% changes the actual game value')
        repeated=command('statraw '+key+' increased 1')
        check(int(re.search(r'gameValue=(-?\d+)',repeated)[1])==before*2, f'attribute {sp}: repeated application does not accumulate')
        reset=command('statraw '+key+' reset')
        check(int(re.search(r'gameValue=(-?\d+)',reset)[1])==before, f'attribute {sp}: reset preserves baseline')
    command('statraw 21 0 0 0 added 2')
    combined=command('statraw 21 0 0 0 increased 1')
    check(int(re.search(r'gameValue=(-?\d+)',combined)[1])==(base+2)*2, 'flat and percent bonuses compose in the real character')
    command('statraw 21 0 0 0 reset')
    command('sheetopen 1')
    time.sleep(.4) # The game's lazy sheet prefab is created on later normal frames.
    command('statraw 21 0 0 0 increased 1')
    time.sleep(.15)
    sheet=command('sheetstats')
    report['intelligenceSheetRows']=[x for x in sheet.splitlines() if 'SP=21 (Intelligence)' in x]
    check(any('name=IntStat ' in x and f'cachedText={base*2} ' in x for x in report['intelligenceSheetRows']), 'visible game character sheet confirms Intelligence doubled')
    command('statraw 21 0 0 0 reset')
    command('framereset')
    bridge=UiBridge(); delays=[]
    for _ in range(20):
        t=time.perf_counter()
        result=bridge.execute('raw_stat', {'sp':21,'tags':0,'special':0,'extra':0,'mode':'increased','unit':'percent'},'read')
        assert result['ok']
        delays.append(round((time.perf_counter()-t)*1000,2))
    report['statReadMs']={'minimum':min(delays),'maximum':max(delays),'mean':sum(delays)/len(delays),'samples':delays}
    check(max(delays)<500, '20 complete stat reads finish below 500 ms each')
    check('progressread' not in [x['command'] for x in report['transcript'][-20:]], 'stat reading does not request quest or map catalogs')
    report['statFrames']=json.loads(command('frameread'))
    before=json.loads(command('progressread'))
    check(before['rewards']['level']==8, 'campaign test begins at level 8, without completed owner progress')
    command('framereset')
    result=json.loads(command('questscomplete '+sid))
    report['campaignResult']=result
    report['campaignFrames']=json.loads(command('frameread'))
    check(result['ok'] and result['after']['level']==55 and result['remaining']==0, 'normal quest rewards and level 55 reached')
    check(result['completed']>=80 and result['steps']>result['completed'], 'rewarded quests and level-up events yield between frames')
    check(result['after']['passivePoints']==15 and result['after']['idolUnlock']==8, 'real quest passive and idol rewards verified')
    check(report['campaignFrames']['maxStepMs']<100, 'no campaign main-thread step exceeds 100 ms')
    check(report['campaignFrames']['maxFrameGapMs']<100, 'actual campaign frame gap stays below 100 ms')
    elapsed=next(x['elapsedMs'] for x in report['transcript'] if x['command']=='questscomplete '+sid)
    check(elapsed<10000, 'full rewarded campaign and level top-up complete in under 10 seconds')
    check(result['skippedSyntheticAnalytics']>0, 'only artificial bulk-progression analytics are suppressed')
    check(progression.validate_backup(Path(result['backup']))['manifest']['id']==sid, 'backup with live character/stash/global validates before recovery')
    repeat=json.loads(command('questscomplete '+sid))
    check(repeat['ok'] and repeat['completed']==0 and repeat['levelTopUpXp']==0 and repeat['before']==repeat['after'], 'repeat action gives no duplicate rewards or experience')
    command('framereset')
    waypoint=json.loads(command('waypointsunlock '+sid))
    report['waypointFrames']=json.loads(command('frameread'))
    check(waypoint['ok'], 'all-waypoint action succeeds')
    final=json.loads(command('progressread'))
    check(all(p['unlocked'] for p in final['waypoints'] if not p['noWaypoint']), 'every actual waypoint is unlocked in game readback')
    report.update(ok=True, releaseReady=True, identity=identity, finalRewards=final['rewards'])
    save()
    return {k:v for k,v in report.items() if k not in ('transcript','intelligenceSheetRows')}


if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('action',choices=['prepare','verify','restore']);args=a.parse_args()
    print(json.dumps(prepare() if args.action=='prepare' else verify() if args.action=='verify' else isolated.restore(),indent=2))
