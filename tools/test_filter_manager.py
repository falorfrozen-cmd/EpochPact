import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from tools import filter_manager as fm

FILTER = '''<ItemFilter xmlns:i="http://www.w3.org/2001/XMLSchema-instance">
  <name>{name}</name>
  <filterIcon>0</filterIcon>
  <description />
  <lastModifiedInVersion>1.4</lastModifiedInVersion>
  <rules>
    <Rule><type>HIDE</type><isEnabled>true</isEnabled></Rule>
    <Rule><type>SHOW</type><isEnabled>false</isEnabled></Rule>
  </rules>
</ItemFilter>
'''


class FilterManagerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        base = Path(self.temp.name)
        self.root = base / 'Filters'
        self.root.mkdir()
        self.outside = base / 'downloads'
        self.outside.mkdir()
        self.out = base / 'out'
        (self.root / 'Sorcerer.xml').write_text(FILTER.format(name='Sorcerer'), encoding='utf-8')
        (self.root / 'steam_autocloud.vdf').write_text('x', encoding='utf-8')

    def tearDown(self):
        self.temp.cleanup()

    def test_list_counts_rules_and_ignores_other_files(self):
        result = fm.list_filters(self.root)
        self.assertEqual([f['file'] for f in result['filters']], ['Sorcerer.xml'])
        self.assertEqual((result['filters'][0]['rules'], result['filters'][0]['enabled']), (2, 1))

    def test_damaged_filter_is_listed_as_invalid(self):
        (self.root / 'Broken.xml').write_text('<ItemFilter><name>x', encoding='utf-8')
        broken = [f for f in fm.list_filters(self.root)['filters'] if f['file'] == 'Broken.xml'][0]
        self.assertFalse(broken['valid'])

    def test_import_never_replaces_an_existing_filter(self):
        source = self.outside / 'Sorcerer.xml'
        source.write_text(FILTER.format(name='Sorcerer'), encoding='utf-8')
        before = (self.root / 'Sorcerer.xml').read_bytes()
        result = fm.import_filter(str(source), self.root)
        self.assertEqual(result['imported'], ['Sorcerer (2)'])
        self.assertEqual((self.root / 'Sorcerer.xml').read_bytes(), before)
        imported = (self.root / 'Sorcerer (2).xml').read_text(encoding='utf-8')
        self.assertIn('<name>Sorcerer (2)</name>', imported)
        self.assertIn('<Rule><type>SHOW</type>', imported)

    def test_import_refuses_doctype_and_non_filters(self):
        bomb = self.outside / 'bomb.xml'
        bomb.write_text('<?xml version="1.0"?><!DOCTYPE x [<!ENTITY a "aaaa">]><ItemFilter><name>&a;</name><rules/></ItemFilter>', encoding='utf-8')
        other = self.outside / 'other.xml'
        other.write_text('<Settings><name>x</name></Settings>', encoding='utf-8')
        for path in (bomb, other):
            with self.assertRaises(ValueError):
                fm.import_filter(str(path), self.root)
        with self.assertRaises(ValueError):
            fm.import_filter('relative.xml', self.root)
        self.assertEqual(len(list(self.root.glob('*.xml'))), 1)

    def test_import_sanitizes_the_file_name(self):
        source = self.outside / 'shared.xml'
        source.write_text(FILTER.format(name='CON'), encoding='utf-8')
        self.assertEqual(fm.import_filter(str(source), self.root)['imported'], ['Imported filter'])
        source.write_text(FILTER.format(name='a/b:c* &amp; d'), encoding='utf-8')
        name = fm.import_filter(str(source), self.root)['imported'][0]
        self.assertEqual(name, 'abc & d')
        self.assertIn('<name>abc &amp; d</name>', (self.root / 'abc & d.xml').read_text(encoding='utf-8'))

    def test_duplicate_keeps_the_original(self):
        before = (self.root / 'Sorcerer.xml').read_bytes()
        result = fm.duplicate_filter('Sorcerer.xml', self.root)
        self.assertEqual(result['name'], 'Sorcerer copy')
        self.assertEqual((self.root / 'Sorcerer.xml').read_bytes(), before)
        self.assertEqual(fm.duplicate_filter('Sorcerer.xml', self.root)['name'], 'Sorcerer copy (2)')
        for bad in ('../Sorcerer.xml', 'missing.xml', 'steam_autocloud.vdf', None):
            with self.assertRaises(ValueError):
                fm.duplicate_filter(bad, self.root)

    def test_export_writes_a_new_file_outside_the_game_folder(self):
        first = fm.export_filter('Sorcerer.xml', self.root, self.out)
        second = fm.export_filter('Sorcerer.xml', self.root, self.out)
        self.assertEqual(Path(first['path']).name, 'Sorcerer.xml')
        self.assertEqual(Path(second['path']).name, 'Sorcerer (2).xml')
        self.assertEqual(Path(first['path']).read_bytes(), (self.root / 'Sorcerer.xml').read_bytes())

    def test_backup_round_trip_restores_as_new_filters(self):
        (self.root / 'Necro.xml').write_text(FILTER.format(name='Necro'), encoding='utf-8')
        backup = fm.backup_filters(self.root, self.out)
        self.assertEqual(backup['count'], 2)
        with zipfile.ZipFile(backup['path']) as archive:
            self.assertEqual(sorted(archive.namelist()), ['Necro.xml', 'Sorcerer.xml'])
        restored = fm.import_filter(backup['path'], self.root)
        self.assertEqual(sorted(restored['imported']), ['Necro (2)', 'Sorcerer (2)'])

    def test_zip_with_one_bad_member_writes_nothing(self):
        archive_path = self.outside / 'mixed.zip'
        with zipfile.ZipFile(archive_path, 'w') as archive:
            archive.writestr('Good.xml', FILTER.format(name='Good'))
            archive.writestr('Bad.xml', '<NotAFilter/>')
        with self.assertRaises(ValueError):
            fm.import_filter(str(archive_path), self.root)
        self.assertFalse((self.root / 'Good.xml').exists())

    def test_backups_are_pruned(self):
        with patch.object(fm, 'BACKUPS_KEPT', 2):
            for _ in range(4):
                fm.backup_filters(self.root, self.out)
        self.assertEqual(len(list(self.out.glob('filters-*.zip'))), 2)

    def test_launcher_routes_filter_actions(self):
        from tools import player_setup
        setup = player_setup.PlayerSetup.__new__(player_setup.PlayerSetup)
        opened = []
        setup.reveal = opened.append
        with patch.object(fm, 'filters_root', return_value=self.root), patch.object(fm, 'user_root', return_value=self.out):
            self.assertEqual(len(setup.filters('filters', {})['filters']), 1)
            self.assertTrue(setup.filters('filter_backup', {})['ok'])
            self.assertTrue(setup.filters('filter_export', {'file': 'Sorcerer.xml'})['ok'])
            self.assertTrue(setup.filters('filter_folder', {})['ok'])
        self.assertEqual(opened[-1], self.root)
        self.assertEqual(len(opened), 3)


if __name__ == '__main__':
    unittest.main()
