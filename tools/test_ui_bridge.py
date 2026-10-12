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
    def test_material_stock_requires_current_identity_and_retains_backup(self):
        self.live['craftmaterials 42 10000']={'ok':True,'backup':'test-only-snapshot','updatedKinds':12,'target':10000}
        result=self.bridge.execute('craft_materials', {'expected_id':'42','value':10000})
        self.assertTrue(result['ok']);self.assertEqual(result['backup'],'test-only-snapshot')
        self.assertEqual(self.commands.count('craftmaterials 42 10000'),1)
        self.commands.clear()
        with self.assertRaisesRegex(RuntimeError,'character has changed'):
            self.bridge.execute('craft_materials', {'expected_id':'41','value':10000})
        self.assertFalse(any(x.startswith('craftmaterials ') for x in self.commands))

    def test_material_invalid_quantity_never_reaches_ipc(self):
        for value in (True,0,1000001,1.5):
            with self.subTest(value=value),self.assertRaises(ValueError):
                self.bridge.execute('craft_materials', {'expected_id':'42','value':value})
        self.assertEqual(self.commands,[])

    def test_material_stock_refuses_a_reused_save_slot_with_a_different_name(self):
        with self.assertRaisesRegex(RuntimeError,'character has changed'):
            self.bridge.execute('craft_materials', {'expected_id':'42','expected_name':'Previous owner','value':10000})
        self.assertFalse(any(x.startswith('craftmaterials ') for x in self.commands))

    def test_choose_mastery_opens_only_the_native_view_for_current_character(self):
        self.live['masteryopen 42'] = {'ok': True, 'opened': True, 'player': dict(self.player)}
        result = self.bridge.execute('mastery_choose', {'expected_id': '42'})
        self.assertTrue(result['opened'])
        self.assertEqual(self.commands, ['sessionread', 'playerread', 'identityread', 'masteryopen 42'])
        self.assertFalse(any(x.startswith(('questscomplete', 'waypointsunlock')) for x in self.commands))

    def test_choose_mastery_refuses_stale_identity_before_opening_any_view(self):
        with self.assertRaisesRegex(RuntimeError, 'character has changed'):
            self.bridge.execute('mastery_choose', {'expected_id': '41'})
        self.assertFalse(any(x.startswith('masteryopen') for x in self.commands))

    def test_choose_mastery_preserves_native_unavailable_error(self):
        self.live['masteryopen 42'] = {'ok': False, 'error': 'A mastery is already selected.'}
        result = self.bridge.execute('mastery_choose', {'expected_id': '42'})
        self.assertFalse(result['ok'])
        self.assertIn('already selected', result['error'])
        self.assertEqual(self.commands.count('masteryopen 42'), 1)

    def test_build_settings_validate_identity_before_mutation(self):
        self.bridge.execute('build_skillxp', {'value': 3, 'expected_id': '42', 'expected_name': 'Test'})
        self.assertEqual(self.commands, ['sessionread', 'playerread', 'identityread', 'buildset skillxp 3'])
        self.commands.clear()
        with self.assertRaisesRegex(RuntimeError, 'character has changed'):
            self.bridge.execute('build_projectileflat', {'value': 2, 'expected_id': '41'})
        self.assertFalse(any(x.startswith('buildset') for x in self.commands))

    def test_build_bounds_and_blessing_choices_reject_before_ipc(self):
        for cid,value in [('build_skillxp',101),('build_zoom',2.1),('build_weaverflat',.5),
                          ('build_projectileflat',33),('build_blessingoptions',1),('build_blessingoptions',2)]:
            with self.subTest(cid=cid,value=value), self.assertRaises(ValueError):
                self.bridge.execute(cid, {'value':value})
        self.assertEqual(self.commands, [])

    def test_custom_crafting_requires_current_character(self):
        with self.assertRaisesRegex(RuntimeError, 'character has changed'):
            self.bridge.execute('craft_custom', {'template_id':-1, 'affixes':'0,1', 'perfect':1, 'expected_id':'41'})
        self.assertFalse(any(x.startswith('craftcustom') for x in self.commands))

    def test_custom_crafting_rejects_eight_or_duplicate_affixes_before_ipc(self):
        for csv in ('0,1,2,3,4,5,6,7','1,1','1,-1','1;status'):
            with self.subTest(csv=csv), self.assertRaises(ValueError):
                self.bridge.execute('craft_custom', {'template_id':-1,'affixes':csv,'perfect':1,'expected_id':'42'})
        self.assertEqual(self.commands, [])

    def test_custom_crafting_dispatches_once_and_retains_backup(self):
        self.live['craftcustom 42 -1 0,1,2,3,4,5,6 1']={'ok':True,'backup':'test-only-snapshot'}
        result=self.bridge.execute('craft_custom', {'template_id':-1,'affixes':'0,1,2,3,4,5,6','perfect':1,'expected_id':'42'})
        self.assertEqual(result['backup'],'test-only-snapshot')
        self.assertEqual(self.commands.count('craftcustom 42 -1 0,1,2,3,4,5,6 1'),1)

    def test_workshop_catalog_only_reads_and_refuses_identity_change(self):
        self.live['atlasread']={'ok':True,'player':dict(self.player),'items':[{'id':5,'name':'Example','set':True}]}
        result=self.bridge.execute('craft_custom_catalog', {})
        self.assertEqual(result['items'][0]['id'],5)
        self.assertEqual(self.commands[-5:],['atlasread','lootread','identityread','craftmods','identityread'])
        self.live['atlasread']['player']['id']='41'
        with self.assertRaisesRegex(RuntimeError,'character changed'):
            self.bridge.execute('craft_custom_catalog', {})
        self.assertFalse(any(x.startswith('craftcustom') for x in self.commands))

    def test_mixed_modifier_recipe_dispatches_once_and_retains_recovery(self):
        cmd='craftmix 42 -1 0,1 1 u0:0,u1:1,s1:0'
        self.live[cmd]={'ok':True,'backup':'mixed-item-backup'}
        result=self.bridge.execute('craft_custom', {'template_id':-1,'affixes':'0,1','perfect':1,'modifiers':'u0:0,u1:1,s1:0','expected_id':'42'})
        self.assertEqual(result['backup'],'mixed-item-backup')
        self.assertEqual(self.commands.count(cmd),1)

    def test_invalid_mixed_recipe_never_reaches_native_ipc(self):
        for selection in ('u0:0,u0:0','u0:0;status','u0:0 s1:0',','.join(f'u{x}:0' for x in range(65))):
            with self.subTest(selection=selection), self.assertRaises(ValueError):
                self.bridge.execute('craft_custom', {'template_id':-1,'affixes':'0','perfect':1,'modifiers':selection,'expected_id':'42'})
        with self.assertRaises(ValueError):
            self.bridge.execute('craft_custom', {'template_id':-1,'affixes':'0,1,2,3,4,5,6','perfect':1,'modifiers':'u0:0','expected_id':'42'})
        self.assertEqual(self.commands,[])

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
        # Action contracts below start after a completed authenticated handshake.
        # Connection-specific tests always perform the real handshake path.
        self.bridge._verified_session = '123:100'
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
        self.live['craftmods'] = {'ok':True,'modifiers':[]}
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
                         patch.object(ui.le, 'selected_game_process', return_value={'processId':123,'sessionKey':'123:100','startedNs':0}),
                         patch.object(ui.le, 'ipc_ready', return_value=True),
                         patch.object(ui.le, 'send', side_effect=self.send)]
        for p in self.patchers:
            p.start()
            self.addCleanup(p.stop)

    def send(self, command, **kwargs):
        import json
        self.commands.append(command)
        if command == 'connectionping':
            return json.dumps({'ok':True,'protocol':3,**{k:self.bridge.process[k] for k in ('processId','sessionKey')}})
        if command in self.live:
            return json.dumps(self.live[command])
        if command.startswith('stashread'):
            return json.dumps({'ok': True, 'player': dict(self.player), 'scope': 'test-stash', 'revision': 'test-revision',
                               'total': 0, 'tabs': 1, 'offset': 0, 'next': None, 'items': [], 'readMs': 0})
        if command.startswith('monolithpanelready'):
            return json.dumps({'ok':True,'player':dict(self.player),'panelReady':True})
        if command.startswith('masteryopen '):
            return json.dumps({'ok':True,'opened':True,'player':dict(self.player)})
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
        if command.startswith(('qolread','qolset ','pickupradius','pickupinterval ','pickupnow','lootlog','summonread','inventoryread','inventoryaction ')):
            return json.dumps({'ok':True,'player':dict(self.player),'settings':{},'summons':[],'radius':0})
        if command.startswith(('buildset ', 'buildread')):
            return json.dumps({'ok': True, 'settings': {}})
        if command.startswith('treerespec '):
            return json.dumps({'ok': True, 'player': dict(self.player), 'refunded': 3, 'backup': 'test-only-snapshot'})
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
                    elif name == 'affixes':
                        args[name] = 'keep'
                    elif kind == 'optional_single_token':
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
        for category in ('materials', 'gold', 'potions', 'xp', 'favor', 'bones', 'amber'):
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

    def test_connection_recovers_after_first_unity_frame_timeout_without_mutation(self):
        self.live['sessionread'] = {'ok':False, 'error':'the main thread did not pick the job up'}
        pending = self.bridge.connection()
        self.assertTrue(pending['ok']); self.assertTrue(pending['connected'])
        self.assertFalse(pending['offline']); self.assertEqual(pending['phase'], 'waiting_frame')
        self.assertEqual(self.commands, ['connectionping','status','sessionread'])
        self.live['sessionread'] = {'ok':True,'state':'InGame','transitioning':False}
        ready = self.bridge.connection()
        self.assertTrue(ready['offline']); self.assertEqual(ready['phase'], 'ready')
        self.assertEqual(ready['sessionKey'], '123:100')
        self.assertTrue(all(x in ('connectionping','status','sessionread','identityread','statraw 9 0 0 0') for x in self.commands))

    def test_missing_protocol_does_not_block_authenticated_connection(self):
        with patch.object(ui.le, 'ipc_ready', return_value=False):
            result = self.bridge.connection()
            self.assertEqual(result['phase'], 'ready'); self.assertTrue(result['connected'])
        self.assertEqual(self.commands[0], 'connectionping')

    def test_handshake_failure_prevents_mutation(self):
        self.bridge._verified_session = None
        self.live['connectionping'] = {'ok':False,'error':'native unavailable'}
        # Override the handshake fixture with a failed actual response.
        with patch.object(ui.le,'send',return_value=json.dumps(self.live['connectionping'])):
            with self.assertRaisesRegex(RuntimeError,'native unavailable'):
                self.bridge.execute('xp', {'value':2})
        self.assertNotIn('xp 2',self.commands)

    def test_connection_assigns_an_independent_native_compatible_timeout_to_each_read(self):
        clock = [100.0]; calls=[]
        def send(command, *, timeout):
            calls.append((command,timeout)); clock[0] += 2
            return self.send(command)
        with patch.object(ui.time, 'monotonic', side_effect=lambda:clock[0]), patch.object(ui.le, 'send', side_effect=send):
            result = self.bridge.connection()
        self.assertTrue(result['offline'])
        self.assertEqual([x[1] for x in calls], [8,8,8,8,8])

    def test_ancillary_speed_read_failure_does_not_disconnect_verified_character(self):
        original = self.send
        with patch.object(ui.le, 'send', side_effect=lambda c,**kw: None if c.startswith('statraw') else original(c)):
            result = self.bridge.connection()
        self.assertTrue(result['offline']); self.assertEqual(result['phase'],'ready')
        self.assertNotIn('speed',result['values'])

    def test_ancillary_speed_reply_with_wrong_owner_revokes_connection(self):
        original = self.send
        def send(command, **kwargs):
            if command.startswith('statraw'):
                raise ui.le.GameSessionError('wrong session')
            return original(command, **kwargs)
        with patch.object(ui.le, 'send', side_effect=send):
            result = self.bridge.connection()
        self.assertFalse(result['connected']); self.assertFalse(result['offline'])
        self.assertIsNone(result['player']); self.assertIsNone(self.bridge._verified_session)

    def test_nested_backend_requests_inherit_the_verified_process_binding(self):
        bindings = []
        original = self.send
        def send(command, **kwargs):
            owner = ui.le._ipc_process.get()
            bindings.append(owner() if callable(owner) else owner)
            return original(command, **kwargs)
        with patch.object(ui.le, 'send', side_effect=send):
            self.bridge.execute('craft_custom_catalog', {}, 'read')
        self.assertTrue(bindings)
        self.assertTrue(all(x['sessionKey'] == '123:100' for x in bindings))
        self.assertIsNone(ui.le._ipc_process.get())

    def test_restarting_same_character_discards_old_cleanup_and_rejects_queued_action(self):
        self.bridge.connection()
        self.bridge.temporary_resets[('xp',None)]='xp 1'
        self.bridge.actor['21:0:0:0']={'key':{'sp':21,'tags':0,'special':0,'extra':0},'modes':{'added':20}}
        self.bridge.cache['progress_read']={'player':self.player}
        self.commands.clear()
        with patch.object(ui.le, 'selected_game_process', return_value={'processId':124,'sessionKey':'124:200','startedNs':0}):
            with self.assertRaisesRegex(RuntimeError,'game restarted'):
                self.bridge.execute('campaign_complete',{'expected_id':'42','expected_session':'123:100'})
            self.assertFalse(self.bridge.actor or self.bridge.temporary_resets or self.bridge.cache)
            self.assertEqual(self.bridge.restore_session()['restored'],0)
            self.assertEqual(self.commands,['connectionping'])
            result=self.bridge.connection(); self.assertEqual(result['sessionKey'],'124:200')

    def test_identity_failure_cannot_enable_offline_controls(self):
        self.live['identityread']={'ok':False,'error':'Character is transitioning'}
        result=self.bridge.connection()
        self.assertTrue(result['connected']); self.assertFalse(result['offline'])
        self.assertIsNone(result['player']); self.assertEqual(result['phase'],'character_loading')

    def test_metadata_failure_is_not_reported_as_a_frame_timeout(self):
        self.live['sessionread']={'ok':False,'error':'client session metadata unavailable'}
        result=self.bridge.connection()
        self.assertTrue(result['connected']); self.assertFalse(result['offline'])
        self.assertEqual(result['phase'],'read_error')

    def test_changed_reply_owner_revokes_the_dll_connection(self):
        original=self.send
        def send(command, **kwargs):
            if command == 'sessionread': raise ui.le.GameSessionError('wrong session')
            return original(command, **kwargs)
        with patch.object(ui.le,'send',side_effect=send): result=self.bridge.connection()
        self.assertFalse(result['connected']); self.assertEqual(result['phase'],'unavailable')
        self.assertIsNone(self.bridge._verified_session)

    def test_stopped_game_returns_immediately_and_clears_owned_state(self):
        self.bridge.connection(); self.bridge.temporary_resets[('xp',None)]='xp 1'; self.commands.clear()
        with patch.object(ui.le, 'selected_game_process', return_value=None):
            result=self.bridge.connection()
        self.assertEqual(result['phase'],'stopped'); self.assertFalse(result['connected'])
        self.assertEqual(self.commands,[]); self.assertFalse(self.bridge.temporary_resets)

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
    def test_every_script_referenced_by_the_index_is_served(self):
        index = self.client.get('/').get_data(as_text=True)
        scripts = re.findall(r'<script[^>]+src="([^"]+)"', index)
        self.assertIn('/ui/affix-picker.js', scripts)
        for path in scripts:
            with self.client.get(path) as response:
                self.assertEqual(response.status_code, 200, path)
                self.assertEqual(response.data, (ui.ROOT / path.lstrip('/')).read_bytes(), path)

    def setUp(self):
        self.bridge = ui.UiBridge(preview=True)
        self.app = create_app(self.bridge)
        self.addCleanup(self.app.extensions['epoch_worker'].shutdown)
        self.client = self.app.test_client()
        self.headers = {'X-Epoch-Token':self.client.get('/api/bootstrap').json['token']}

    def test_preferences_script_is_served_with_the_player_ui(self):
        with self.client.get('/ui/crafting-model.js') as crafting_response:
            self.assertEqual(crafting_response.status_code, 200)
            self.assertIn(b'EpochCrafting', crafting_response.data)
        with self.client.get('/ui/preferences.js') as response:
            self.assertEqual(response.status_code, 200)
            self.assertIn(b'EpochPreferences', response.data)
        with self.client.get('/') as response:
            self.assertIn(b'/ui/preferences.js', response.data)

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

    def test_expired_native_backup_disappears_from_recovery_history(self):
        with tempfile.TemporaryDirectory() as folder:
            boundary = Path(folder); kept = boundary / 'kept'; kept.mkdir()
            (kept / 'manifest.json').write_text('{}')
            self.bridge.history = [
                {'id': 0, 'backup': str(boundary / 'expired')},
                {'id': 1, 'backup': str(kept)},
            ]
            with patch.object(ui.progression, 'BACKUPS', boundary):
                self.assertEqual(self.bridge.available_history(), [{'id': 1, 'backup': str(kept)}])
                with patch.object(ui.progression, 'undo') as undo:
                    with self.assertRaises(ValueError):
                        self.bridge.execute('undo', {'backup': str(boundary / 'expired')})
                    undo.assert_not_called()

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
