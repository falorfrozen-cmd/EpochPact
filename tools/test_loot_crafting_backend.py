import unittest
from unittest.mock import patch
from tools import loot_crafting_backend as backend


class Controls(unittest.TestCase):
    def test_invalid_requests_never_reach_ipc(self):
        with patch.object(backend.ipc, "request") as request:
            for call in (lambda: backend.loot_mode("all\ncraftfp 0"), lambda: backend.minimum_lp(True),
                         lambda: backend.minimum_lp(5), lambda: backend.affixes([True]),
                         lambda: backend.affixes_csv("1,2\ncraftshards 1"), lambda: backend.affixes(list(range(65))),
                         lambda: backend.fp_factor(float("nan")), lambda: backend.fp_factor(-1),
                         lambda: backend.glyph_chance("hope", float("inf")), lambda: backend.glyph_chance("chaos", 100),
                         lambda: backend.glyph_chance("despair", 101), lambda: backend.category("runes", True),
                         lambda: backend.preserve_shards(0.5)):
                with self.assertRaises(ValueError): call()
            request.assert_not_called()

    def test_reset_and_boundaries(self):
        with patch.object(backend.ipc, "request", return_value={"ok": True}) as request:
            backend.fp_factor(0); backend.fp_factor(1)
            backend.glyph_chance("hope", 0); backend.glyph_chance("despair", 100)
            backend.glyph_chance("hope", None); backend.affixes([1, 2, 1]); backend.affixes([])
            self.assertEqual([c.args[0] for c in request.call_args_list],
                             ["craftfp 0", "craftfp 1", "crafthope 0", "craftdespair 100", "crafthope reset", "lootaffixes 1,2", "lootaffixes none"])


class ExtendedControlsTests(unittest.TestCase):
    def test_new_toggles_reject_non_binary_values_without_ipc(self):
        with patch.object(backend.ipc, "request") as request:
            for function in (backend.preserve_runes, backend.preserve_glyphs, backend.bypass_level, backend.reveal_map):
                for value in (2, -1, 0.5, float("nan"), "1\ncraftforge 0", None):
                    with self.subTest(function=function.__name__, value=value), self.assertRaises(ValueError):
                        function(value)
            request.assert_not_called()

    def test_new_toggle_commands_and_forge_submit_exactly_once(self):
        with patch.object(backend.ipc, "request", return_value={"ok": True}) as request:
            backend.preserve_runes(True); backend.preserve_glyphs(False); backend.bypass_level(1); backend.reveal_map(0)
            backend.map_read(); backend.forge("0")
            self.assertEqual([c.args[0] for c in request.call_args_list],
                             ["craftrunes 1", "craftglyphs 0", "craftlevel 1", "mapreveal 0", "mapread", "craftforge 0"])

    def test_forge_requires_explicit_valid_save_identity(self):
        with patch.object(backend.ipc, "request") as request:
            for identity in (None, 0, "", "-1", "0 craftforge 0", "0\nquestscomplete 0"):
                with self.subTest(identity=identity), self.assertRaises(ValueError): backend.forge(identity)
            request.assert_not_called()


if __name__ == "__main__": unittest.main()
