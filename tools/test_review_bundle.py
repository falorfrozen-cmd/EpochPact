"""A changed or unexpected runtime must never enter a review ZIP."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
from tools.package_nexus_release import sha, verify_archive, safe_archive_name
from tools.package_review_bundle import read_bundle, package
from tools.desktop_config import NAME, validate


class ReviewBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='EpochPact-review-package-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root / 'EpochPact'
        resources = self.bundle / '_internal'
        core = resources / 'native/build/player/EpochPact.Core.dll'
        core.parent.mkdir(parents=True)
        core.write_bytes(b'MZ non-runnable player fixture')
        loader = resources / 'native/build/player/version.dll'
        loader.write_bytes(b'MZ non-runnable loader fixture')
        bridge = resources / 'native/build/melon/EpochPact.MelonBridge.dll'
        bridge.parent.mkdir()
        bridge.write_bytes(b'MZ non-runnable managed bridge fixture')
        (core.parent / 'build-info.json').write_text(json.dumps({'flavor': 'player', 'sha256': sha(core.read_bytes()),
                                                                'loaderSha256': sha(loader.read_bytes())}))
        (resources / 'ui/assets').mkdir(parents=True)
        (resources / 'ui/catalog.json').write_text(json.dumps({'liveState': {}, 'currentValues': {}}))
        (resources / 'ui/supported-game-builds.json').write_text('{"builds":[]}')
        (resources / 'ui/assets/inter-OFL.txt').write_text('isolated license fixture')
        (resources / 'THIRD-PARTY-NOTICES.txt').write_text('isolated notices fixture')
        (self.bundle / 'EpochPact.exe').write_bytes(b'MZ non-runnable application fixture')
        (self.bundle / NAME).write_bytes(validate(Path(__file__).resolve().parents[1] / NAME))
        self.info = dict(version='fixture-review.1', layout='onedir', containsResearchBuild=False,
                         containsHistoricalSnapshots=False, sha256=sha((self.bundle / 'EpochPact.exe').read_bytes()),
                         nativePlayerSHA256=sha(core.read_bytes()), loaderSHA256=sha(loader.read_bytes()))
        self.stamp()

    def stamp(self):
        self.info['files'] = [dict(path=p.relative_to(self.bundle).as_posix(), bytes=p.stat().st_size, sha256=sha(p.read_bytes()))
                              for p in sorted(self.bundle.rglob('*')) if p.is_file() and p.name != 'build-info.json']
        # Include the native build-info.json; only the outer inventory is excluded.
        native = self.bundle / '_internal/native/build/player/build-info.json'
        self.info['files'].append(dict(path=native.relative_to(self.bundle).as_posix(),
                                       bytes=native.stat().st_size, sha256=sha(native.read_bytes())))
        (self.bundle / 'build-info.json').write_text(json.dumps(self.info))

    def add(self, name, data=b'non-runnable fixture'):
        path = self.bundle / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.stamp()

    def test_valid_bundle_is_verified_and_existing_archive_is_preserved(self):
        output = self.root / 'archives'
        result = package(self.bundle, output)
        archive = output / result['file']
        self.assertEqual(verify_archive(archive)['sha256'], result['sha256'])
        before = archive.read_bytes()
        with self.assertRaises(FileExistsError):
            package(self.bundle, output)
        self.assertEqual(archive.read_bytes(), before)

    def test_player_zip_keeps_matching_manual_runtime_and_preserves_existing_archive(self):
        from tools.package_player_folder import package as player_package
        result = player_package(self.bundle, self.root / 'player')
        archive = self.root / 'player' / result['file']
        before = archive.read_bytes()
        with zipfile.ZipFile(archive) as entries:
            self.assertNotIn('ManualReview', archive.name)
            self.assertFalse(any(name.startswith('CopyToGame/') for name in entries.namelist()))
            self.assertEqual(entries.read('Manual Installation/version.dll'),
                             entries.read('EpochPact/_internal/native/build/player/version.dll'))
            self.assertNotIn('Manual Installation/EpochPact/EpochPact.Core.dll', entries.namelist())
            self.assertEqual(entries.read('EpochPact/' + NAME), (self.bundle / NAME).read_bytes())
            self.assertNotIn(b'{{VERSION}}', entries.read('README.md'))
            self.assertEqual(json.loads(entries.read('runtime-manifest.json'))['kind'], 'Player')
        self.assertEqual(verify_archive(archive)['sha256'], result['sha256'])
        with self.assertRaises(FileExistsError):
            player_package(self.bundle, self.root / 'player')
        self.assertEqual(archive.read_bytes(), before)

        revised = player_package(self.bundle, self.root / 'player', revision='package.1')
        self.assertEqual(revised['version'], result['version'])
        self.assertEqual(revised['file'], 'EpochPact-fixture-review.1-package.1.zip')
        with zipfile.ZipFile(self.root / 'player' / revised['file']) as entries:
            self.assertEqual(json.loads(entries.read('runtime-manifest.json'))['packageRevision'], 'package.1')
            self.assertEqual(entries.read('EpochPact/EpochPact.exe'), (self.bundle / 'EpochPact.exe').read_bytes())
        self.assertEqual(archive.read_bytes(), before)

    def test_same_size_executable_tampering_is_rejected(self):
        exe = self.bundle / 'EpochPact.exe'
        exe.write_bytes(b'X' * exe.stat().st_size)
        with self.assertRaisesRegex(ValueError, 'digest mismatch'):
            read_bundle(self.bundle)

    def test_unlisted_runtime_is_rejected(self):
        (self.bundle / 'unexpected.dll').write_bytes(b'non-runnable')
        with self.assertRaisesRegex(ValueError, 'Unexpected or missing'):
            read_bundle(self.bundle)

    def test_missing_clr_configuration_is_rejected_even_if_inventory_matches(self):
        (self.bundle / NAME).unlink()
        self.stamp()
        with self.assertRaisesRegex(ValueError, 'Missing or invalid EpochPact.exe.config'):
            read_bundle(self.bundle)

    def test_disabled_clr_configuration_is_rejected_even_if_inventory_matches(self):
        path = self.bundle / NAME
        path.write_bytes(path.read_bytes().replace(b'enabled="true"', b'enabled="false"'))
        self.stamp()
        with self.assertRaisesRegex(ValueError, 'Invalid EpochPact.exe.config'):
            read_bundle(self.bundle)

    def test_jinja_template_predicates_remain_in_runtime(self):
        self.add('_internal/jinja2/tests.pyc')
        _, files = read_bundle(self.bundle)
        self.assertIn('_internal/jinja2/tests.pyc', files)

    def test_save_nested_archive_and_build_tool_are_rejected_even_if_hashes_match(self):
        for name in ('_internal/character.sav', '_internal/base_library.zip', '_internal/setuptools/__init__.pyc'):
            with self.subTest(name=name):
                self.add(name)
                with self.assertRaises(ValueError):
                    read_bundle(self.bundle)
                (self.bundle / name).unlink()
                self.stamp()

    def test_research_flavor_is_rejected_even_with_matching_digest(self):
        native = self.bundle / '_internal/native/build/player/build-info.json'
        value = json.loads(native.read_text()); value['flavor'] = 'research'
        native.write_text(json.dumps(value)); self.stamp()
        with self.assertRaisesRegex(ValueError, 'Player core mismatch'):
            read_bundle(self.bundle)

    def test_windows_unsafe_and_aliased_paths_are_rejected(self):
        for name in ('../file.dll', '/file.dll', 'C:/file.dll', 'safe\\file.dll',
                     'safe/../file', 'safe./file', 'safe /file', 'NUL.txt', 'COM1/file', './file', 'file:stream'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                safe_archive_name(name)

    def test_case_colliding_zip_entries_are_rejected_before_manifest_use(self):
        path = self.root / 'case.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('App.dll', b'A'); archive.writestr('app.dll', b'B')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            verify_archive(path)

    def test_junction_bundle_root_does_not_read_or_modify_target(self):
        link = self.root / 'linked-bundle'
        subprocess.run(['cmd', '/d', '/c', 'mklink', '/J', str(link), str(self.bundle)],
                       capture_output=True, check=True)
        self.addCleanup(link.rmdir)
        before = (self.bundle / 'EpochPact.exe').read_bytes()
        with self.assertRaisesRegex(ValueError, 'Linked bundle root'):
            read_bundle(link)
        self.assertEqual((self.bundle / 'EpochPact.exe').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
