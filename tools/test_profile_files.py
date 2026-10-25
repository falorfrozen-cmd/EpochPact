import json
import tempfile
import unittest
from pathlib import Path

from tools import profile_files


class ProfileFileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='EpochPact-profiles-')
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_export_then_import_keeps_name_and_settings(self):
        result = profile_files.export_profile('Balanced', {'xp': 3, 'autopickup': True, 'craft_hope': None}, output=self.root)
        self.assertEqual(result['file'], 'Balanced.json')
        data = json.loads(Path(result['path']).read_text(encoding='utf-8'))
        self.assertEqual(data, {'epochpact': 'profile', 'version': 1, 'name': 'Balanced',
                                'settings': {'xp': 3, 'autopickup': True, 'craft_hope': None}})
        imported = profile_files.import_profile(result['path'])
        self.assertEqual(imported, {'ok': True, 'name': 'Balanced', 'settings': {'xp': 3, 'autopickup': True, 'craft_hope': None}})

    def test_export_never_replaces_a_file(self):
        first = profile_files.export_profile('Balanced', {'xp': 3}, output=self.root)
        second = profile_files.export_profile('Balanced', {'xp': 4}, output=self.root)
        self.assertEqual((first['file'], second['file']), ('Balanced.json', 'Balanced (2).json'))
        self.assertEqual(json.loads(Path(first['path']).read_text(encoding='utf-8'))['settings'], {'xp': 3})

    def test_unsafe_names_make_a_safe_file_name(self):
        result = profile_files.export_profile('a/b:c*?', {'xp': 2}, output=self.root)
        self.assertEqual(result['file'], 'abc.json')
        self.assertEqual(profile_files.export_profile('CON', {'xp': 2}, output=self.root)['file'], 'Profile.json')

    def test_import_keeps_only_data(self):
        path = self.root / 'shared.json'
        path.write_text(json.dumps({'epochpact': 'profile', 'version': 1, 'name': '  My   build ',
                                    'settings': {'xp': 3, 'gold': 'run calc.exe', 'BAD KEY': 1, 'drops': 2, 'rarity': float('inf'),
                                                 'density': [1], 'speed': 1.5}}), encoding='utf-8')
        imported = profile_files.import_profile(f'"{path}"')  # Explorer's "Copy as path" adds quotes
        self.assertEqual(imported['name'], 'My build')
        self.assertEqual(imported['settings'], {'xp': 3, 'drops': 2, 'speed': 1.5})

    def test_import_refuses_other_files(self):
        cases = {'not-json.json': 'hello', 'filter.json': json.dumps({'name': 'x', 'settings': {'xp': 2}}),
                 'empty.json': json.dumps({'epochpact': 'profile', 'version': 1, 'name': 'x', 'settings': {'BAD': 1}})}
        for name, text in cases.items():
            (self.root / name).write_text(text, encoding='utf-8')
            with self.assertRaises(ValueError, msg=name):
                profile_files.import_profile(str(self.root / name))
        (self.root / 'profile.txt').write_text('{}', encoding='utf-8')
        for path in ('', 'relative.json', str(self.root / 'missing.json'), str(self.root / 'profile.txt')):
            with self.assertRaises(ValueError, msg=path):
                profile_files.import_profile(path)
        big = self.root / 'big.json'
        big.write_text(' ' * (profile_files.FILE_LIMIT + 1), encoding='utf-8')
        with self.assertRaises(ValueError):
            profile_files.import_profile(str(big))

    def test_launcher_routes_profile_actions(self):
        from unittest.mock import patch
        from tools import player_setup
        setup = player_setup.PlayerSetup.__new__(player_setup.PlayerSetup)
        opened = []
        setup.reveal = opened.append
        with patch.object(profile_files, 'user_root', return_value=self.root):
            exported = setup.profiles('profile_export', {'name': 'Speed run', 'settings': {'speed': 2}})
            self.assertEqual(setup.profiles('profile_import', {'path': exported['path']})['settings'], {'speed': 2})
        self.assertEqual(opened, [self.root / 'profile-exports'])
        self.assertIn('profile_export', player_setup.PROFILE_ACTIONS)


if __name__ == '__main__':
    unittest.main()
