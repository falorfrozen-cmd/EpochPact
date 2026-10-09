"""UI integration contract tests; all native IPC is mocked, no game/save access."""
import copy
import json
import re
import threading
import time
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from . import ui_bridge as ui
from .ui_language import english_exception, english_message
from epochpact_ui import create_app


class BridgeTests(unittest.TestCase):
    def test_automatic_general_settings_verify_the_character_before_dispatch(self):
        self.bridge.execute('xp', {'value': 2, 'expected_id': '42', 'expected_name': 'Test'})
        self.assertEqual(self.commands, ['sessionread', 'playerread', 'identityread', 'xp 2'])
        for args in ({'expected_id': '7', 'expected_name': 'Test'},
                     {'expected_id': '42', 'expected_name': 'Another character'}):
            self.commands.clear()
            with self.assertRaisesRegex(RuntimeError, 'character has changed'):
                self.bridge.execute('xp', {'value': 2, **args})
            self.assertNotIn('xp 2', self.commands)

    def test_automatic_speed_reuses_the_verified_identity_without_double_reading(self):
        self.bridge.execute('speed', {'value': 2, 'expected_id': '42', 'expected_name': 'Test'})
        self.assertEqual(self.commands.count('identityread'), 1)
        self.assertIn('speed 2', self.commands)

    def test_automatic_expected_name_is_validated_before_any_ipc(self):
        with self.assertRaises(ValueError):
            self.bridge.execute('xp', {'value': 2, 'expected_id': '42', 'expected_name': []})
        self.assertEqual(self.commands, [])

    def test_stat_read_uses_only_identity_and_stat_without_quest_scan(self):
        self.bridge.execute('raw_stat', {'sp':21,'tags':0,'special':0,'extra':0,'mode':'increased','unit':'percent'}, 'read')
        self.assertEqual(self.commands, ['identityread', 'statraw 21 0 0 0'])

    def test_stat_read_refuses_native_offline_identity_failure(self):
        self.live['identityread'] = {'ok':False, 'error':'not offline'}
        with self.assertRaisesRegex(RuntimeError, 'not offline'):
            self.bridge.execute('raw_stat', {'sp':21,'tags':0,'special':0,'extra':0,'mode':'increased','unit':'percent'}, 'read')
        self.assertEqual(self.commands, ['identityread'])

    def setUp(self):
        self.bridge = ui.UiBridge()
        self.commands = []
        self.live = copy.deepcopy(self.bridge.catalog['liveState'])
        self.player = {'id': '42', 'name': 'Test', 'scene': 'M_Rest'}
        for data in self.live.values():
            if isinstance(data, dict) and 'player' in data:
                data['player'] = dict(self.player)
        self.live['sessionread'] = {'ok': True, 'state': 'InGame', 'transitioning': False}
        self.live['identityread'] = {'ok': True, 'offline': True, 'player': self.live['progressread']['player']}
        self.live.pop('sheetstats', None)  # Snapshot has an unavailable C-panel reply, not a success fixture.
        self.live['monolithread']['editable'] = True
        self.live['monolithread']['restContextReady'] = True
        self.live['atlasread'] = {'ok': True, 'player': dict(self.player), 'items': []}
        for t in self.live['monolithread']['timelines']:
            for d in t['difficulties']:
                d['unlocked'] = True
        self.live['cofread']['cof'].update(member=True, rank=12)
        for slot in self.live['cofread']['slots']:
            slot['locked'] = False
        for r in self.live['cofread']['rewards']:
            r['available'] = True
        for lens in self.live['cofread']['lenses']:
            lens['purchased'] = True
        self.patchers = [patch.object(ui.le, 'game_pids', return_value=[123]),
                         patch.object(ui.le, 'send', side_effect=self.send)]
        for p in self.patchers:
            p.start()
            self.addCleanup(p.stop)

    def send(self, command, **kwargs):
        import json
        self.commands.append(command)
        if command in self.live:
            return json.dumps(self.live[command])
        if command.startswith('stashread'):
            return json.dumps({'ok': True, 'player': dict(self.player), 'scope': 'test-stash', 'revision': 'test-revision',
                               'total': 0, 'tabs': 1, 'offset': 0, 'next': None, 'items': [], 'readMs': 0})
        if command.startswith('monolithpanelready'):
            return json.dumps({'ok':True,'player':dict(self.player),'panelReady':True})
        if command.startswith(('echoread', 'echofocus')):
            return json.dumps({'ok': True, 'player': dict(self.player), 'echoes': [], 'generated': False})
        if command == 'playerread':
            return 'playerread: name=Test level=55 CharacterData.IsOffline=true'
        if command == 'status':
            return 'EpochPact; gate: offline play [client only]\nxp: x2, refused online 0\nspeed: x5, modifier +400%\nautopickup: off'
        if command.startswith('statraw'):
            return 'statraw: EpochPact added=0 increased=0.5 more=0 attached=1'
        if command.split()[0] in {'questscomplete', 'waypointsunlock', 'monolithunlock', 'monolithselect', 'corruption', 'stability', 'cofjoin', 'cofrank', 'coffavor', 'cofreputation', 'coflenses', 'cofprophecy', 'cofcharges'}:
            return json.dumps({'ok': True, 'backup': 'test-only-snapshot'})
        if command.startswith(('cof', 'stabilitymult', 'loot', 'craft', 'map')):
            return json.dumps({'ok': True})
        return command.split()[0] + ': accepted'

    def test_bootstrap_does_not_touch_game_or_return_snapshot(self):
        app = create_app(self.bridge)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        response = app.test_client().get('/api/bootstrap')
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json['catalog']['counts']['controls'], len(self.bridge.catalog['controls']))
        self.assertNotIn('liveState', response.json['catalog'])
        self.assertNotIn('currentValues', response.json['catalog'])
        self.assertEqual(self.commands, [])

    def test_density_uses_existing_session_command_in_echo_and_rejects_out_of_bounds(self):
        self.player['scene'] = 'M_M200'
        self.live['monolithread']['editable'] = False
        for value in (1, 2.5, 5):
            self.commands.clear()
            self.assertTrue(self.bridge.execute('density', {'value': value})['ok'])
            self.assertEqual(self.commands, ['sessionread', 'playerread', f'density {value:g}'])
        for invalid in (0, 5.1, float('inf'), True):
            self.commands.clear()
            with self.assertRaises(ValueError):
                self.bridge.execute('density', {'value': invalid})
            self.assertEqual(self.commands, [])

    def test_connection_reads_density_from_current_native_status(self):
        def density_status(command, **kwargs):
            if command == 'status':
                return 'EpochPact; gate: offline play [client only]\ndensity: x2.5, hooks in, boosted 12, refused online 0'
            return self.send(command, **kwargs)
        with patch.object(ui.le, 'send', side_effect=density_status):
            live = self.bridge.connection()
        self.assertTrue(live['connected'])
        self.assertEqual(live['values']['density'], 2.5)

    def test_echo_read_does_not_refresh_world_map_or_start_an_echo(self):
        self.bridge.execute('echo_read', {'expected_id': '42', 'timeline': 1, 'difficulty': 'normal'}, 'read')
        self.assertIn('echoread 42 1 normal', self.commands)
        self.assertNotIn('progressread', self.commands)
        self.assertFalse(any(c.startswith(('monolithselect', 'travel', 'questscomplete')) for c in self.commands))

    def test_echo_missing_identity_refuses_before_echo_command(self):
        with self.assertRaises(RuntimeError):
            self.bridge.execute('echo_focus', {'timeline': 1, 'difficulty': 'normal', 'index': 2})
        self.assertFalse(any(c.startswith('echofocus') for c in self.commands))

    def test_collection_preview_does_not_invent_ownership(self):
        bridge = ui.UiBridge(preview=True)
        for cid in ('atlas_read', 'stash_read'):
            with self.assertRaisesRegex(RuntimeError, 'loaded offline character'):
                bridge.execute(cid, operation='read')
        self.assertEqual(self.commands, [])

    def test_english_catalog_covers_all_features_without_changing_contract(self):
        original = copy.deepcopy(self.bridge.catalog)
        public = self.bridge.public_catalog()
        locale = json.loads((ui.ROOT / 'ui/locale-en.json').read_text(encoding='utf-8'))
        self.assertEqual(public['language'], 'en')
        for section in ('controls', 'groups', 'researchOnly'):
            self.assertEqual(set(locale[section]), {entry['id'] for entry in original[section]})
            for entry in public[section]:
                for key in ('label', 'description', 'details'):
                    if key in entry:
                        self.assertTrue(entry[key])
                        self.assertIsNone(re.search('[çğıöşüÇĞİÖŞÜ]', entry[key]))
        for before, after in zip(original['controls'], public['controls']):
            self.assertEqual({k:v for k,v in before.items() if k not in ('label', 'description')},
                             {k:v for k,v in after.items() if k not in ('label', 'description')})
        self.assertEqual(self.bridge.catalog, original)
        self.assertEqual(self.commands, [])

    def test_all_catalog_controls_route_to_existing_ipc(self):
        timeline = self.live['monolithread']['timelines'][0]
        for control in self.bridge.catalog['controls']:
            cid = control['id']
            with self.subTest(control=cid):
                args = {}
                for p in control['parameters']:
                    name, kind = p['name'], p['type']
                    if name == 'save_id':
                        args['expected_id'] = '42'
                    elif name == 'row':
                        args['catalog_index'] = 0
                    elif name == 'name':
                        args[name] = 'strength' if cid == 'stat_alias' else 'Test'
                    elif name == 'timeline':
                        args[name] = timeline['id']
                    elif name == 'difficulty':
                        args[name] = 'normal'
                    elif name == 'mode':
                        args[name] = 'all' if cid == 'loot_mode' else 'added'
                    elif name == 'ids':
                        args[name] = 'none'
                    elif kind == 'enum' and p.get('choices'):
                        args[name] = p['choices'][0]
                    elif name == 'sp':
                        args[name] = 9
                    elif name == 'backup':
                        args[name] = 'test-only-snapshot'
                    elif kind == 'integer_or_none':
                        args[name] = None
                    else:
                        args[name] = control['default'] if name == 'value' and control['default'] is not None else max(p.get('minimum', 0), 0)
                if cid == 'play_offline':
                    self.live['sessionread']['state'] = 'Login'
                elif cid == 'load_character':
                    self.live['sessionread']['state'] = 'CharacterSelect'
                else:
                    self.live['sessionread']['state'] = 'InGame'
                if cid == 'undo':
                    with patch.object(ui.progression, 'undo', return_value={'ok': True}) as undo:
                        self.assertTrue(self.bridge.execute(cid, args)['ok'])
                        undo.assert_called_once()
                else:
                    before = len(self.commands)
                    operation = 'read' if control['widget'] in ('read', 'preview') else 'set'
                    self.assertTrue(self.bridge.execute(cid, args, operation)['ok'])
                    self.assertGreater(len(self.commands), before)

    def test_invalid_numeric_inputs_emit_no_ipc(self):
        for control in self.bridge.catalog['controls']:
            if control['lifetime'] != 'session' or control['widget'] != 'number':
                continue
            p = control['parameters'][0]
            for v in (True, float('nan'), float('inf'), p['minimum']-1, p['maximum']+1, '2\nquestscomplete 42'):
                with self.subTest(id=control['id'], value=v), self.assertRaises(ValueError):
                    self.bridge.execute(control['id'], {'value': v})
        self.assertEqual(self.commands, [])

    def test_pickup_category_reset_formats_each_actual_category(self):
        for category in ('materials', 'gold', 'potions', 'xp', 'favor', 'bones'):
            self.bridge.execute('loot_category', {'category': category}, 'reset')
            self.assertEqual(self.commands[-1], f'lootcategory {category} 1')

    def test_selector_resets_do_not_require_the_old_selection(self):
        for control, command in (('loot_mode', 'lootmode all'), ('loot_affixes', 'lootaffixes none')):
            self.bridge.execute(control, {}, 'reset')
            self.assertEqual(self.commands[-1], command)

    def test_forge_preview_survives_ui_simulation_flag(self):
        forge = {"hasItem": True, "canForge": True, "forgingPotential": 30,
                 "costRange": {"nativeMinimum": 1, "nativeMaximum": 18}, "exactOutcomeKnown": False}
        self.live['craftread']['preview'] = copy.deepcopy(forge)
        result = self.bridge.execute('craft_read', {}, 'read')
        self.assertEqual(result['forgePreview'], forge)
        self.assertIsNot(result.get('preview'), True)
        demo = ui.UiBridge(preview=True)
        demo.demo['craftread']['preview'] = copy.deepcopy(forge)
        self.assertEqual(demo.execute('craft_read', {}, 'read')['forgePreview'], forge)
        demo.execute('craft_hope', {'value': 100})
        self.assertEqual(demo.execute('craft_read', {}, 'read')['hopePercent'], 100)
        demo.execute('craft_hope', {'value': None})
        self.assertIsNone(demo.execute('craft_read', {}, 'read')['hopePercent'])
        self.assertEqual(self.commands[-1], 'craftread')

    def test_forge_failure_keeps_recovery_and_never_repeats_the_craft(self):
        reply={'ok':False,'backup':'forge-snapshot','error':'Craft may have completed; refund failed'}
        original=ui.progression.request
        with patch.object(ui.progression,'request',side_effect=lambda command,**kw: reply if command.startswith('craftforge ') else original(command,**kw)) as request:
            result=self.bridge.execute('craft_forge', {'expected_id':self.player['id']})
        self.assertEqual([c.args[0] for c in request.call_args_list].count('craftforge 42'),1)
        for key,value in reply.items():self.assertEqual(result[key],value)
        self.assertEqual(self.bridge.history[-1]['backup'],'forge-snapshot')
        self.assertFalse(self.bridge.history[-1]['ok'])

    def test_percent_alias_and_raw_units_are_distinct(self):
        self.bridge.execute('stat_alias', {'name':'allres','value':65,'unit':'percent'})
        self.assertAlmostEqual(float(self.commands[-1].split()[-1]), .65)
        self.bridge.execute('stat_alias', {'name':'strength','value':80,'unit':'raw'})
        self.assertEqual(float(self.commands[-1].split()[-1]), 80)
        self.bridge.execute('cof_double_enemy', {'value':65})
        self.assertEqual(self.commands[-1], 'cofdouble enemy 65')
        self.bridge.execute('cof_double_enemy', {'value':None})
        self.assertEqual(self.commands[-1], 'cofdouble enemy reset')
        before = len(self.commands)
        with self.assertRaises(ValueError):
            self.bridge.execute('stat_alias', {'name':'strength','value':80,'unit':'percent'})
        self.assertEqual(len(self.commands), before)

    def test_all_sheet_rows_use_full_keys_including_secondary(self):
        preview = ui.UiBridge(preview=True)
        for i, row in enumerate(preview.catalog['characterStats']['sheetRows']):
            for secondary in (False, True) if row['modifierKey'] else (False,):
                result = preview.execute('sheet_row', {'catalog_index':i,'secondary':secondary,'mode':'added','value':1})
                key = row['modifierKey'] if secondary else row['key']
                self.assertIn(ui.raw_command(key)+' added 1', result['text'])
        self.assertEqual(self.commands, [])

    def test_all_72_aliases_and_134_properties_are_editable(self):
        preview = ui.UiBridge(preview=True)
        for alias in preview.catalog['characterStats']['aliases']:
            r = preview.execute('stat_alias', {'name':alias['name'],'value':1})
            self.assertEqual(r['sharedKey'], alias['sharedKey'])
        for prop in preview.catalog['characterStats']['properties']:
            r = preview.execute('raw_stat', {'sp':prop['id'],'tags':0,'special':0,'extra':0,'mode':'added','value':1})
            self.assertTrue(r['ok'])

    def test_stat_read_reset_and_mode_floor(self):
        self.bridge.execute('stat_alias', {'name':'strength','value':80})
        self.bridge.execute('stat_alias', {'name':'strength'}, 'reset')
        self.assertTrue(self.commands[-1].endswith(' reset'))
        self.assertNotIn('1:0:0:0', self.bridge.actor)
        self.bridge.execute('sheet_row', {'catalog_index':0,'mode':'added'}, 'read')
        before = len(self.commands)
        with self.assertRaises(ValueError):
            self.bridge.execute('raw_stat', {'sp':9,'tags':0,'special':0,'extra':0,'mode':'more','value':-101,'unit':'percent'})
        self.assertEqual(len(self.commands), before)

    def test_preview_reads_shared_bonus_modes_without_sending_ipc(self):
        demo = ui.UiBridge(preview=True)
        stats = demo.catalog['characterStats']
        index = next(i for i, row in enumerate(stats['sheetRows']) if row['objectName'] == 'GuileStat')
        args = {'catalog_index': index, 'mode': 'added'}
        first = demo.execute('sheet_row', args, 'read')
        self.assertIn('EpochPact added=0 increased=0 more=0 attached=1', first['text'])
        demo.execute('sheet_row', {**args, 'value': 15})
        demo.execute('sheet_row', {**args, 'mode': 'increased', 'unit': 'percent', 'value': 25})
        alias = next(a for a in stats['aliases'] if a['sharedKey'] == first['sharedKey'])
        result = demo.execute('stat_alias', {'name': alias['name']}, 'read')
        self.assertIn('EpochPact added=15 increased=0.25 more=0 attached=1', result['text'])
        demo.execute('sheet_row', args, 'reset')
        self.assertIn('EpochPact added=0 increased=0 more=0 attached=1', demo.execute('sheet_row', args, 'read')['text'])
        self.assertEqual(self.commands, [])

    def test_shared_speed_key_keeps_modes_and_rejects_other_actor_replay(self):
        self.bridge.execute('raw_stat', {'sp':9,'tags':0,'special':0,'extra':0,'mode':'added','value':3})
        self.bridge.execute('speed', {'value':2})
        self.assertEqual(self.bridge.actor['9:0:0:0']['modes'], {'added':3,'increased':1})
        self.live['progressread']['player']['id'] = '43'
        with self.assertRaisesRegex(RuntimeError, 'another character'):
            self.bridge.reconcile()
        self.assertNotIn('questscomplete 43', self.commands)

    def test_speed_connection_reads_owned_contribution_not_stale_speed_command(self):
        self.assertEqual(self.bridge.connection()['values']['speed'], 1.5)

    def test_stale_character_cannot_receive_save_mutation(self):
        with self.assertRaisesRegex(RuntimeError, 'character has changed'):
            self.bridge.execute('campaign_complete', {'expected_id':'0'})
        self.assertFalse(any(x.startswith('questscomplete') for x in self.commands))

    def test_empowered_corruption_uses_runtime_lower_bound(self):
        timeline = self.live['monolithread']['timelines'][0]
        with self.assertRaises(ValueError):
            self.bridge.execute('corruption', {'expected_id':'42','timeline':timeline['id'],'difficulty':'empowered','value':0})
        self.assertFalse(any(x.startswith('corruption ') for x in self.commands))
        self.bridge.execute('corruption', {'expected_id':'42','timeline':timeline['id'],'difficulty':'empowered','value':100})
        self.assertTrue(self.commands[-1].endswith('empowered 100'))

    def test_cof_prophecy_previews_before_write_and_preserves_failure_backup(self):
        args = {'expected_id':'42','slot':0,'reward':None,'lens':None}
        self.bridge.execute('cofprophecy', args)
        self.assertEqual(self.commands[-2:], ['cofpreview 42 0 none none','cofprophecy 42 0 none none'])
        with patch.object(ui.progression, 'apply', return_value={'ok':False,'backup':'failed-snapshot','error':'save failed'}):
            result = self.bridge.execute('campaign_complete', {'expected_id':'42'})
        self.assertEqual(result['history'][-1]['backup'], 'failed-snapshot')
        self.assertFalse(result['ok'])

    def test_text_errors_are_not_success(self):
        for r in ('xp: refused: online', 'statraw: error: bad key', None):
            with self.assertRaises(RuntimeError):
                ui.text_ok(r)
        self.assertTrue(ui.text_ok('density: x1, refused online 0')['ok'])

    def test_session_requirement_and_online_gate(self):
        self.live['sessionread']['state'] = 'Login'
        with self.assertRaises(RuntimeError):
            self.bridge.execute('xp', {'value':2})
        self.assertNotIn('xp 2', self.commands)
        self.bridge.execute('play_offline')
        self.assertEqual(self.commands[-1], 'playoffline')
        self.live['sessionread']['state'] = 'InGame'
        with patch.object(ui.le, 'send', side_effect=lambda cmd, **kw: '{"ok":true,"state":"InGame","transitioning":false}' if cmd=='sessionread' else 'playerread: CharacterData.IsOffline=false'):
            with self.assertRaises(RuntimeError):
                self.bridge.execute('gold', {'value':2})


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.bridge = ui.UiBridge(preview=True)
        self.app = create_app(self.bridge)
        self.addCleanup(self.app.extensions['epoch_worker'].shutdown)
        self.client = self.app.test_client()
        self.headers = {'X-Epoch-Token':self.client.get('/api/bootstrap').json['token']}

    def test_player_is_default_and_developer_requires_explicit_opt_in(self):
        self.assertFalse(self.client.get('/api/bootstrap').json['developer'])
        app = create_app(self.bridge, developer=True)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        self.assertTrue(app.test_client().get('/api/bootstrap').json['developer'])

    def test_connection_supplies_recovery_history_without_dispatching_a_save(self):
        self.bridge.history = [{'id':0,'control':'stability','backup':'C:/backup','ok':True,'time':1}]
        with patch.object(self.bridge, 'execute') as execute:
            response = self.client.post('/api/jobs', json={'type':'connection'}, headers=self.headers)
            ident = response.json['job']
            result = self.app.extensions['epoch_jobs'][ident].result(timeout=2)
            self.assertEqual(result['history'], self.bridge.history)
            execute.assert_not_called()

    def test_bounded_local_journal_keeps_native_details_without_session_token(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'operations.log'
            app = create_app(self.bridge, operation_log=log)
            client = app.test_client()
            token = client.get('/api/bootstrap').json['token']
            reply = {'ok': True, 'backup': 'C:/private/backup', 'actorValues': {'9:0:0:0': {}}}
            try:
                with patch.object(self.bridge, 'execute', return_value=reply):
                    response = client.post('/api/jobs', json={'type':'control','id':'stability','args':{'value':400}}, headers={'X-Epoch-Token':token})
                    ident = response.json['job']
                    app.extensions['epoch_jobs'][ident].result(timeout=2)
                    self.assertEqual(client.get('/api/jobs/'+ident).json['result'], reply)
                text = log.read_text(encoding='utf-8')
                self.assertIn('actorValues', text)
                self.assertIn('C:/private/backup', text)
                self.assertNotIn(token, text)
                handler = app.extensions['epoch_journal_handler']
                self.assertEqual(handler.maxBytes, 2 * 1024 * 1024)
                self.assertEqual(handler.backupCount, 2)
            finally:
                app.extensions['epoch_worker'].shutdown()
                app.extensions['epoch_journal_handler'].close()

    def test_origin_token_and_source_protection(self):
        payload = {'type':'connection'}
        self.assertEqual(self.client.post('/api/jobs', json=payload).status_code, 403)
        self.assertEqual(self.client.post('/api/jobs', json=payload, headers={**self.headers,'Origin':'https://evil.example'}).status_code, 403)
        self.assertEqual(self.client.get('/ui/catalog.json').status_code, 404)
        self.assertEqual(self.client.get('/ui/../tools/ui_bridge.py').status_code, 404)
        self.assertEqual(self.client.get('/ui/assets/../catalog.json').status_code, 404)

    def test_restart_rejects_old_session_before_queueing_save_action(self):
        restarted = create_app(self.bridge)
        self.addCleanup(restarted.extensions['epoch_worker'].shutdown)
        client = restarted.test_client()
        with patch.object(self.bridge, 'execute') as execute:
            response = client.post('/api/jobs', json={'type':'control', 'id':'campaign_complete'}, headers=self.headers)
            self.assertEqual(response.status_code, 403)
            self.assertEqual(response.json['error'], 'Invalid interface session.')
            self.assertEqual(len(restarted.extensions['epoch_jobs']), 0)
            execute.assert_not_called()
        with client.get('/ui/session.js') as asset:
            self.assertEqual(asset.status_code, 200)

    def test_http_jobs_are_strictly_serial_and_errors_visible(self):
        active = 0
        maximum = 0
        calls = []
        lock = threading.Lock()
        def execute(cid, args, operation):
            nonlocal active, maximum
            with lock:
                active += 1
                maximum = max(maximum, active)
            time.sleep(.01)
            calls.append(args['value'])
            with lock:
                active -= 1
            if args['value']==3:
                raise RuntimeError('test failure')
            return {'ok':True}
        with patch.object(self.bridge, 'execute', side_effect=execute):
            ids = [self.client.post('/api/jobs', json={'type':'control','id':'xp','args':{'value':i}}, headers=self.headers).json['job'] for i in range(1,5)]
            for ident in ids:
                self.app.extensions['epoch_jobs'][ident].result(timeout=2)
        self.assertEqual(maximum, 1)
        self.assertEqual(calls, [1,2,3,4])
        result = self.client.get('/api/jobs/'+ids[2]).json['result']
        self.assertFalse(result['ok'])
        self.assertEqual(result['error'], 'test failure')

    def test_backend_errors_are_english_without_altering_backup_or_identity(self):
        def job_result():
            response = self.client.post('/api/jobs', json={'type':'control','id':'xp','args':{'value':2}}, headers=self.headers)
            ident = response.json['job']
            self.app.extensions['epoch_jobs'][ident].result(timeout=2)
            return self.client.get('/api/jobs/'+ident).json['result']
        with patch.object(self.bridge, 'execute', side_effect=RuntimeError('Oyun komuta yanıt vermedi.')):
            self.assertEqual(job_result(), {'ok':False,'error':'The game did not respond.'})
        reply = {'ok':False, 'error':'Kayıt yedeği eksik.', 'backup':'C:/Yedek/çağrı',
                 'player':{'name':'Çağrı'}, 'details':[{'error':'CoF bilgisi okunamadı.'}]}
        with patch.object(self.bridge, 'execute', return_value=reply):
            result = job_result()
        self.assertEqual(result['error'], 'The save backup is missing.')
        self.assertEqual(result['details'][0]['error'], 'Could not read Circle of Fortune information.')
        self.assertEqual(result['backup'], reply['backup'])
        self.assertEqual(result['player'], reply['player'])
        self.assertEqual(reply['error'], 'Kayıt yedeği eksik.')

    def test_dynamic_and_os_errors_keep_details_in_english(self):
        self.assertEqual(english_message('Tam sayı 0–99 arasında olmalı.'), 'Enter a whole number between 0 and 99.')
        self.assertEqual(english_message('Geçersiz kayıt: Çağrı.json'), 'Invalid save: Çağrı.json')
        self.assertEqual(english_exception(FileNotFoundError(2, 'Dosya bulunamadı', 'çağrı.json')),
                         'File not found (errno 2): çağrı.json')


if __name__ == '__main__':
    unittest.main()
