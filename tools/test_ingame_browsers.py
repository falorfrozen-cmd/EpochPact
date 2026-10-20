"""Native browser composition checked against the actual desktop backend; no game access."""
import copy
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch
from tools import collection_backend as desktop

ROOT = Path(__file__).resolve().parents[1]

class NativeBrowserParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ.get("EPOCHPACT_BROWSER_TEST", Path(os.environ.get("EPOCHPACT_NATIVE_TESTS", ROOT / "native/build")) / "ingame_browser_test.exe"))
        if not cls.exe.is_file():
            raise unittest.SkipTest(f"Build native browser fixtures first ({cls.exe})")

    def native(self, request):
        result = subprocess.run([str(self.exe), "compose"], input=json.dumps(request).encode(), capture_output=True, check=True)
        return json.loads(result.stdout)

    def snapshot(self):
        items = [{"key": "a", "name": "Alpha", "uniqueId": 7, "type": 1, "subType": 2, "baseType": "Boots", "kind": "Legendary", "lp": 4, "weaversWill": 0, "quantity": 1},
                 {"key": "b", "name": "Alpha", "uniqueId": 7, "type": 1, "subType": 2, "baseType": "Boots", "kind": "Unique", "lp": 2, "weaversWill": 0, "quantity": 2},
                 {"key": "c", "name": "Alpha", "uniqueId": 7, "type": 1, "subType": 2, "baseType": "Boots", "kind": "Unique", "lp": 4, "weaversWill": 18, "quantity": 1},
                 {"key": "d", "name": "Plain", "uniqueId": None, "type": 1, "subType": 2, "baseType": "Boots", "kind": "Equipment", "lp": 0, "weaversWill": 0, "quantity": 1}]
        return {"ok": True, "player": {"id": "clone", "name": "Test"}, "scope": "scope", "revision": "revision", "total": len(items), "tabs": 5,
                "items": items, "pages": 1, "maxPageMs": 1.5}

    def test_atlas_matches_desktop_including_consumed_lp_and_weaver_copies(self):
        stash = self.snapshot()
        catalog = {"ok": True, "player": stash["player"], "items": [{"id": 7, "name": "Alpha"}, {"id": 8, "name": "Missing"}]}
        with patch.object(desktop, "read_items", return_value=stash), patch.object(desktop.ipc, "request", return_value=catalog):
            expected = desktop.atlas()
        actual = self.native({"kind": "atlas", "catalog": catalog, "stash": stash})
        self.assertEqual({k: actual[k] for k in expected}, expected)

    def test_stash_groups_match_desktop(self):
        stash = self.snapshot()
        with patch.object(desktop, "read_items", return_value=copy.deepcopy(stash)):
            expected = desktop.stash()
        actual = self.native({"kind": "stash", "stash": stash})
        expected["groups"].sort(key=lambda x: x["key"])
        actual["groups"].sort(key=lambda x: x["key"])
        self.assertEqual(actual, expected)

    def test_paged_reads_match_and_never_publish_changed_or_incomplete_snapshots(self):
        stash = self.snapshot()
        first = {**stash, "items": stash["items"][:2], "offset": 0, "next": 2, "readMs": 1.25}
        second = {**stash, "items": stash["items"][2:], "offset": 2, "next": None, "readMs": 2.5}
        with patch.object(desktop.ipc, "request", side_effect=[first, second]):
            expected = desktop.read_items()
        self.assertEqual(self.native({"kind": "scan", "pages": [first, second]}), expected)
        for key, value in (("revision", "new"), ("scope", "another"), ("player", {"id": "other"}), ("total", 5), ("offset", 0), ("items", [])):
            with self.subTest(key=key):
                bad = {**second, key: value}
                with patch.object(desktop.ipc, "request", side_effect=[first, bad]):
                    with self.assertRaises(RuntimeError): desktop.read_items()
                self.assertFalse(self.native({"kind": "scan", "pages": [first, bad]})["ok"])

    def test_catalog_owner_mismatch_and_duplicate_ids_are_rejected(self):
        stash = self.snapshot()
        for catalog in ({"ok": True, "player": {"id": "other"}, "items": []},
                        {"ok": True, "player": stash["player"], "items": [{"id": 7}, {"id": 7}]}):
            with patch.object(desktop, "read_items", return_value=stash), patch.object(desktop.ipc, "request", return_value=catalog):
                with self.assertRaises(RuntimeError): desktop.atlas()
            self.assertFalse(self.native({"kind": "atlas", "catalog": catalog, "stash": stash})["ok"])

if __name__ == "__main__":
    unittest.main()
