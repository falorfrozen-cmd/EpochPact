"""Player-only build provenance and a closed runtime allowlist for the new package."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from tools.package_ingame_player import package, verified_loader
from tools.package_nexus_release import sha, verify_archive


class IngamePackageTests(unittest.TestCase):
    def fixture(self, root, flavor='player'):
        build=root/'build';player=build/'player';player.mkdir(parents=True)
        (player/'version.dll').write_bytes(b'MZ renderer fixture')
        (player/'EpochPact.Core.dll').write_bytes(b'MZ separate core not redistributed')
        (player/'build-info.json').write_text(json.dumps(dict(flavor=flavor,
            loaderSha256=sha((player/'version.dll').read_bytes()),sha256=sha((player/'EpochPact.Core.dll').read_bytes()))))
        return build

    def test_test_and_research_flavors_are_rejected(self):
        for flavor in ('test','research'):
            with tempfile.TemporaryDirectory() as temp:
                with self.assertRaisesRegex(ValueError,'player flavor'):
                    verified_loader(self.fixture(Path(temp),flavor))

    def test_modified_loader_and_core_are_rejected(self):
        for name in ('version.dll','EpochPact.Core.dll'):
            with tempfile.TemporaryDirectory() as temp:
                build=self.fixture(Path(temp));file=build/'player'/name
                file.write_bytes(file.read_bytes()+b'x')
                with self.assertRaisesRegex(ValueError,'hashes'):
                    verified_loader(build)

    def test_zip_contains_only_the_merged_runtime_and_verifiable_player_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);build=self.fixture(root)
            (build/'player'/'private.log').write_text('not for players')
            with patch('tools.package_ingame_player._version_strings',return_value={'FileVersion':'0.1.2-dev.37'}):
                result=package(build,root/'out')
                self.assertTrue(result['verified'])
                archive=root/'out'/result['file']
                self.assertEqual(verify_archive(archive)['runtime'],['version.dll'])
                with zipfile.ZipFile(archive) as z:
                    self.assertEqual(set(z.namelist()),{'version.dll','README.md','INTER-OFL.txt',
                        'DISTRIBUTION-PERMISSIONS.txt','supported-game-builds.json','THIRD-PARTY-NOTICES.txt',
                        'runtime-manifest.json','SHA256SUMS.txt'})
                    self.assertNotIn(b'{{VERSION}}',z.read('README.md'))
                    self.assertIn(b'miniz 3.1.2',z.read('THIRD-PARTY-NOTICES.txt'))
                with self.assertRaises(FileExistsError):package(build,root/'out')
