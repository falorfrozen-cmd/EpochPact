"""Every script, stylesheet and asset the pages load is shipped and served: the
PyInstaller spec, the player build stage and the interface server each keep their
own list, and a file missing from one of them only shows up in a packaged build."""
import ast
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAGES = ('index.html', 'feed.html', 'damage.html')


def loaded():
    """ui/ files the pages reference: /ui/<name> and /ui/assets/<name>, also from CSS."""
    names, assets = set(), set()
    texts = [(ROOT / 'ui' / page).read_text(encoding='utf-8') for page in PAGES]
    texts.append((ROOT / 'ui/style.css').read_text(encoding='utf-8'))
    for text in texts:
        for path in re.findall(r'/ui/([A-Za-z0-9_./-]+\.(?:js|css|png|svg|ttf|json))', text):
            (assets if path.startswith('assets/') else names).add(path.removeprefix('assets/'))
    return names, assets


def tuple_after(text, marker, occurrence=1):
    """The string tuple that follows the n-th occurrence of a marker in a source file."""
    start = -1
    for _ in range(occurrence):
        start = text.index(marker, start + 1)
    start = text.index('(', start + len(marker))
    depth = 0
    for end in range(start, len(text)):
        depth += {'(': 1, ')': -1}.get(text[end], 0)
        if depth == 0:
            return set(ast.literal_eval(text[start:end + 1]))
    raise AssertionError(marker)


class UiResourcesTest(unittest.TestCase):
    def test_pages_load_only_files_that_are_packaged_and_served(self):
        names, assets = loaded()
        self.assertIn('damage-styles.js', names)
        self.assertIn('damage-types.png', assets)
        spec = (ROOT / 'EpochPact.spec').read_text(encoding='utf-8')
        build = (ROOT / 'tools/build_player.py').read_text(encoding='utf-8')
        server = (ROOT / 'epochpact_ui.py').read_text(encoding='utf-8')
        verify = (ROOT / 'tools/verify_player_package.py').read_text(encoding='utf-8')
        spec_files, spec_assets = tuple_after(spec, 'ui_files ='), tuple_after(spec, 'assets =')
        build_files, build_assets = tuple_after(build, 'for name in'), tuple_after(build, 'for name in', 2)
        served = tuple_after(server, 'if path in')
        checked = tuple_after(verify, 'for resource in')
        for name in sorted(names):
            with self.subTest(file=name):
                self.assertIn(name, spec_files)
                self.assertIn(name, build_files)
                self.assertTrue(name in served or name == 'style.css' and 'style.css' in served)
                self.assertIn(name, checked)
                self.assertTrue((ROOT / 'ui' / name).is_file())
        for name in sorted(assets):
            with self.subTest(asset=name):
                self.assertIn(name, spec_assets)
                self.assertIn(name, build_assets)
                self.assertTrue((ROOT / 'ui/assets' / name).is_file())


if __name__ == '__main__':
    unittest.main()
