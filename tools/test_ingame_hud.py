"""Native HUD numbers checked against the actual desktop JS, not a reimplementation of it."""
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NativeHudParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ.get("EPOCHPACT_BROWSER_TEST", Path(os.environ.get("EPOCHPACT_NATIVE_TESTS", ROOT / "native/build")) / "ingame_browser_test.exe"))
        if not cls.exe.is_file():
            raise unittest.SkipTest("Build native HUD/browser fixtures first")

    def native(self, request):
        return json.loads(subprocess.run([str(self.exe), "compose"], input=json.dumps(request).encode(), capture_output=True, check=True).stdout)

    def js(self, request, source):
        return json.loads(subprocess.run(["node", "-e", source], input=json.dumps(request).encode(), cwd=ROOT, capture_output=True, check=True).stdout)

    def test_all_metrics_and_elements_match_desktop_without_double_counting_minions(self):
        state = {"ok": True, "active": True, "elapsedSeconds": 25, "combatSeconds": 10, "types": []}
        for name, n in (("You", 1), ("Skeleton", 3), ("Golem", 2)):
            row = {"name": name, "live": 4, "elements": {}}
            for metric in ("dps10", "dps60", "dps600", "dps3600", "total"):
                row[metric] = n * 123
                row["elements"][metric] = [n * i for i in range(7)]
            row.update(average=10*n, combatAverage=25*n)
            state["types"].append(row)
        state["allies"] = {"dps10": 615}  # already included in sources
        for metric in ("dps10", "dps60", "dps600", "dps3600", "average", "combatAverage"):
            for element in (-1, 0, 4, 6):
                request = {"kind": "damage", "state": state, "metric": metric, "element": element}
                actual = self.native(request)
                expected = self.js(request, "let s='';process.stdin.on('data',x=>s+=x);process.stdin.on('end',()=>{let r=JSON.parse(s);process.stdout.write(JSON.stringify(require('./ui/damage-styles.js').model(r.state,r)));});")
                self.assertEqual(actual["total"], expected["total"])
                self.assertEqual([(r["name"], r["dps"], r["values"]) for r in actual["sources"]], [(r["name"], r["dps"], r["values"]) for r in expected["sources"]])

    def test_unknown_damage_values_stay_unknown(self):
        result = self.native({"kind": "damage", "state": {"ok": True, "types": [{"name": "You", "dps10": None}]}})
        self.assertIsNone(result["total"])
        self.assertIsNone(result["sources"][0]["dps"])

    def test_loot_categories_filters_and_grouping_match_desktop(self):
        entries = [{"kind": "gold", "gold": 12}, {"kind": "gold", "gold": 20}, {"kind": "item", "rarity": "Unique", "name": "Relic"},
                   {"kind": "material", "type": 101, "name": "Health"}, {"kind": "material", "type": 101, "name": "Health"},
                   {"kind": "amber", "name": "Soul Ember", "gold": 3}, {"kind": "amber", "name": "XP mote", "gold": 4}]
        for shown in ({}, {"unique": False, "xp": False}, {"gold": False, "shard": False}):
            request = {"kind": "loot", "entries": entries, "show": shown}
            actual = self.native(request)
            expected = self.js(request, "let s='';process.stdin.on('data',x=>s+=x);process.stdin.on('end',()=>{let r=JSON.parse(s),m=require('./ui/loot-log.js');process.stdout.write(JSON.stringify(m.rows(r.entries,{show:r.show,limit:40}).map(x=>({...x,category:m.category(x)}))));});")
            self.assertEqual([{k: r.get(k) for k in ("kind", "name", "gold", "count", "category")} for r in actual],
                             [{k: r.get(k) for k in ("kind", "name", "gold", "count", "category")} for r in expected])
