"""Reproduce Internet-zone CLR blocking without a game, window or security changes."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

from tools.desktop_config import NAME, validate

ROOT = Path(__file__).resolve().parents[1]


class DesktopConfigTests(unittest.TestCase):
    def test_invalid_inherited_console_does_not_prevent_interface_startup(self):
        import epochpact_ui as desktop_ui
        class BrokenConsole:
            def write(self, _):
                raise OSError(22, 'Invalid inherited Windows console handle')
            def flush(self):
                raise OSError(22, 'Invalid inherited Windows console handle')
        server = MagicMock(server_port=18904)
        with patch.object(sys, 'argv', ['EpochPact.exe', '--preview', '--no-open']), \
             patch.object(sys, 'stdout', BrokenConsole()), \
             patch.object(desktop_ui, 'UiBridge'), \
             patch.object(desktop_ui, 'create_app'), \
             patch.object(desktop_ui, 'interface_server', return_value=server):
            desktop_ui.main()
        server.serve_forever.assert_called_once_with()

    def test_missing_or_malformed_config_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / NAME
            for data in (None, b'not XML', b'<configuration/>',
                         b'<configuration><runtime><loadFromRemoteSources enabled="false"/></runtime></configuration>'):
                if data is not None:
                    path.write_bytes(data)
                with self.assertRaises(ValueError):
                    validate(path)

    @unittest.skipUnless(sys.platform == 'win32' and importlib.util.find_spec('pythonnet'),
                         'Windows CLR dependencies required')
    def test_internet_marked_python_runtime_fails_without_config_and_loads_with_it(self):
        import pythonnet
        source = Path(pythonnet.__file__).parent / 'runtime/Python.Runtime.dll'
        data = source.read_bytes()
        with tempfile.TemporaryDirectory(prefix='EpochPact-downloaded-CLR-') as directory:
            root = Path(directory)
            dll = root / 'Python.Runtime.dll'
            dll.write_bytes(data)
            zone = Path(str(dll) + ':Zone.Identifier')
            zone.write_text('[ZoneTransfer]\r\nZoneId=3\r\n', encoding='utf-8')
            # Use the host EXE's ROOT domain, exactly as the frozen desktop
            # does. A child AppDomain's config cannot replace that host config.
            compiler = Path(os.environ['SystemRoot']) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
            host = root / 'EpochPact-clr-probe.exe'
            source_file = root / 'Probe.cs'
            source_file.write_text('''
using System;
using System.Reflection;
class Probe {
    static int Main(string[] args) {
        try {
            var assembly = Assembly.LoadFrom(args[0]);
            var loader = assembly.GetType("Python.Runtime.Loader", true);
            if (loader.GetMethod("Initialize") == null) return 2;
            Console.WriteLine("RESOLVED");
            return 0;
        } catch (Exception error) {
            Console.Error.WriteLine("HRESULT 0x" + error.HResult.ToString("X8"));
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
''', encoding='utf-8')
            compiled = subprocess.run([str(compiler), '/nologo', '/target:exe',
                                       '/out:' + str(host), str(source_file)],
                                      capture_output=True, text=True, timeout=20)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            blocked = subprocess.run([str(host), str(dll)],
                                     capture_output=True, text=True, timeout=20)
            self.assertNotEqual(blocked.returncode, 0)
            self.assertIn('HRESULT 0x80131515', blocked.stderr)
            config = Path(str(host) + '.config')
            config.write_bytes(validate(ROOT / NAME))
            loaded = subprocess.run([str(host), str(dll)],
                                    capture_output=True, text=True, timeout=20)
            self.assertEqual(loaded.returncode, 0, loaded.stderr)
            self.assertIn('RESOLVED', loaded.stdout)
            self.assertEqual(hashlib.sha256(dll.read_bytes()).digest(), hashlib.sha256(data).digest())
            self.assertIn('ZoneId=3', zone.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
