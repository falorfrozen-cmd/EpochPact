"""The in-game window shows the desktop app's own pages, settings and words.

tools/ingame_pages.py mirrors renderPage() of ui/app.js and tools/build_ingame_assets.py turns it
into the embedded ui.json; these tests keep both tied to the desktop sources, and check that every
image and text the native window asks for is in the built data.
"""
import json
import re
import tempfile
import unittest
from pathlib import Path

from tools import build_ingame_assets as assets
from tools import ingame_pages

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "ui/app.js").read_text(encoding="utf-8")
NATIVE = (ROOT / "native/core/ingame_panel.cpp").read_text(encoding="utf-8")
CATALOG = json.loads((ROOT / "ui/catalog.json").read_text(encoding="utf-8"))
CONTROLS = {c["id"]: c for c in CATALOG["controls"]}


def app_pages():
    """The `pages` table of app.js: id -> [nav, symbol, title, subtitle]."""
    found = re.findall(r'(?m)^\s*(\w+): \["([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)"\],?\s*$', APP)
    return {page: list(rest) for page, *rest in found}


def app_panel_ids(title):
    """The control ids of settingPanel("<title>", ...) in app.js, read from its argument."""
    # two panels have functions of their own: weaverPanel() and ragePanel()
    if title == "Weaver's Will":
        return re.findall(r'"(\w+)"', re.search(r'const weaverSettings=\[([^\]]*)\];', APP)[1])
    if title == "Rage of Morditas (Season 5)":
        return re.findall(r'"(\w+)"', re.search(r'function ragePanel\(\)\{\s*const list=\[([^\]]*)\]\.map\(controlById\)', APP)[1])
    at = APP.find(f'settingPanel("{title}",')
    if at < 0:
        raise AssertionError(f"app.js has no settingPanel {title!r}")
    rest = APP[at + len(f'settingPanel("{title}",'):]
    literal = re.match(r'\[((?:"[\w]+",?)+)\]\.map\(controlById\)', rest)
    if literal:
        return re.findall(r'"(\w+)"', literal[1])
    prefixed = re.match(r'\[((?:"[\w]+",?)+)\]\.map\(key=>controlById\("build_"\+key\)\)', rest) or \
        re.match(r'ids\(\[((?:"[\w]+",?)+)\]\)', rest)
    if prefixed:
        return ["build_" + key for key in re.findall(r'"(\w+)"', prefixed[1])]
    calls = re.match(r'\[((?:controlById\("\w+"\),?)+)\]', rest)
    if calls:
        return re.findall(r'controlById\("(\w+)"\)', calls[1])
    if rest.startswith("[density,controlById(\"stability_multiplier\")]"):
        return ["density", "stability_multiplier"]
    # The filters, as app.js writes them; ingame_pages applies the same ones.
    filters = {
        'catalog.controls.filter(c=>c.group==="general"&&["number","toggle"].includes(c.widget)&&!c.id.startsWith("loot_")'
        '&&!c.id.startsWith("craft_")&&!c.id.startsWith("map_")&&!weaverSettings.includes(c.id))': ingame_pages.general_ids(CATALOG["controls"]),
        'catalog.controls.filter(c=>c.group==="display"&&c.id.startsWith("qol_"))':
            [c["id"] for c in CATALOG["controls"] if c["group"] == "display" and c["id"].startswith("qol_")],
        'catalog.controls.filter(c=>c.group==="cof"&&c.lifetime==="session")':
            [c["id"] for c in CATALOG["controls"] if c["group"] == "cof" and c["lifetime"] == "session"],
    }
    for expression, ids in filters.items():
        if rest.startswith(expression):
            return ids
    raise AssertionError(f"settingPanel {title!r}: unknown form {rest[:120]!r}")


class PagesTests(unittest.TestCase):
    def test_pages_match_the_desktop_table(self):
        desktop = app_pages()
        self.assertEqual([p[0] for p in ingame_pages.PAGES], list(desktop), "same pages in the same order")
        for page, nav, symbol, title, subtitle in ingame_pages.PAGES:
            self.assertEqual([nav, symbol, title, subtitle], desktop[page], page)

    def test_every_settings_panel_matches_app_js(self):
        checked = 0
        for page, *_ in ingame_pages.PAGES:
            for section in ingame_pages.sections(page, CATALOG["controls"], CATALOG["counts"]):
                if section["kind"] != "settings":
                    continue
                expected = [i for i in app_panel_ids(section["title"]) if i in CONTROLS]
                self.assertEqual(section["controls"], expected, f"{page}: {section['title']}")
                for words in (section.get("text"), section.get("note")):
                    if words:
                        self.assertIn(words, APP, f"{page}: {section['title']}: the app's own words")
                for i in section["controls"]:
                    self.assertIn(i, CONTROLS, f"{page}: {section['title']}: {i}")
                checked += 1
        self.assertGreaterEqual(checked, 20)

    def test_automatic_settings_follow_app_js(self):
        # automaticSetting(): the general group on General/Overview, or one of these commands
        source = re.search(r'function automaticSetting\(c\)\{return Boolean\(\(\(\["general","overview"\]\.includes\(page\)&&c\?\.group==="general"\)'
                           r'\|\|/\^\(([^)]*)\) /\.test', APP)
        self.assertIsNotNone(source, "automaticSetting() changed in app.js")
        self.assertEqual(set(source[1].split("|")), {t.strip() for t in assets.AUTOMATIC_TEMPLATES})


class BuiltDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="EpochPact-ingame-assets-")
        cls.out = Path(cls.temp.name)
        cls.data = assets.build(cls.out)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_every_listed_file_is_built_and_embedded(self):
        rc = (self.out / "ingame.rc").read_text(encoding="ascii")
        for name, digest in self.data["files"].items():
            path = self.out / name
            self.assertTrue(path.is_file(), name)
            self.assertEqual(assets.hashlib.sha256(path.read_bytes()).hexdigest(), digest, name)
            self.assertIn(assets.resource_name(name) + " RCDATA", rc, name)
        self.assertIn("EP_UI_JSON RCDATA", rc)

    def test_every_image_the_window_draws_exists(self):
        files = set(self.data["files"])
        named = set(re.findall(r'"([\w-]+\.(?:png|jpg))"', NATIVE))
        named |= {f"icon-{c['icon']}.png" for c in self.data["controls"].values()}
        named |= {f"fill-r{r}.png" for r in re.findall(r'Fill\([^;]*?, (\d+)\)', NATIVE) if r != "0"}
        named |= {f"frame-r{r}.png" for r in re.findall(r'Outline\([^;]*?, (\d+)\)', NATIVE)} | {"frame-r0.png"}
        self.assertFalse(named - files, f"missing images: {sorted(named - files)}")
        for name in files:
            if name.startswith(("frame-", "fill-", "band-")):
                self.assertIn(name.rsplit(".", 1)[0], self.data["slices"], name)

    def test_every_page_and_control_is_complete(self):
        self.assertEqual(len(self.data["pages"]), len(ingame_pages.PAGES))
        self.assertRegex(self.data["appVersion"], r"^\d+\.\d+\.\d+")
        for page in self.data["pages"]:
            for section in page["sections"]:
                if section["kind"] == "settings":
                    self.assertEqual(len(section["automatic"]), len(section["controls"]))
                    for i in section["controls"]:
                        self.assertIn(i, self.data["controls"])
        for c in self.data["controls"].values():
            self.assertTrue(c["command"] and "{value}" in c["command"], c["id"])
            self.assertIn(c["widget"], ("number", "toggle", "choice"), c["id"])
            if c["widget"] != "choice":
                self.assertTrue(c["defaultText"], c["id"])
            if c["widget"] == "choice":
                self.assertEqual(len(c["choices"]), len(c["choiceLabels"]), c["id"])

    def test_words_are_the_desktop_translations(self):
        tr, ru = self.data["i18n"]["tr"], self.data["i18n"]["ru"]
        dictionary = json.loads((ROOT / "ui/i18n-tr.json").read_text(encoding="utf-8"))
        for text in ("Apply changes", "Discard draft", "Default", "Changes apply automatically", "Customize navigation", "General settings"):
            self.assertEqual(tr[text], dictionary[text], text)
            self.assertIn(text, ru, text)
        # whole texts the app builds, translated the way the app translates them
        self.assertEqual(tr["1–100× · Default: 1"], "1–100× · Varsayılan: 1")
        self.assertIn("{0}", tr["{0} settings in draft"])
        self.assertTrue(tr["● Offline · InGame"].startswith(dictionary["● Offline"]))
        # every text the app's dictionary has word for word is shown in the same words
        for language, table in (("tr", tr), ("ru", ru)):
            words = json.loads((ROOT / f"ui/i18n-{language}.json").read_text(encoding="utf-8"))
            for text in assets.texts_of(self.data):
                if words.get(text) and words[text] != text:
                    self.assertEqual(table.get(text), words[text], f"{language}: {text}")


if __name__ == "__main__":
    unittest.main()
