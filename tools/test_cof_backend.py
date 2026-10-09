import unittest
from unittest.mock import patch
from . import cof_backend as cof


class CoFBoundaryTests(unittest.TestCase):
    def test_invalid_values_never_reach_ipc(self):
        with patch.object(cof.progression, "request") as request:
            bad = (
                lambda: cof.join("6\nquestscomplete 0"), lambda: cof.join("٦"),
                lambda: cof.join("6", switch_from_merchant=1),
                lambda: cof.rank("6", True), lambda: cof.rank("6", 13),
                lambda: cof.favor("6", 1000000), lambda: cof.favor("6", -1),
                lambda: cof.reputation("6", 1000001), lambda: cof.charges("6", 0, 100),
                lambda: cof.prophecy("6", 4, 2, None), lambda: cof.prophecy("6", 0, "2", None),
                lambda: cof.prophecy("6", 0, 2, 12), lambda: cof.prophecy("6", 0, True, None),
                lambda: cof.multiplier(float("nan")), lambda: cof.multiplier(True),
                lambda: cof.multiplier(float("inf")), lambda: cof.multiplier(101),
                lambda: cof.reputation_multiplier(True), lambda: cof.reputation_multiplier(0.5),
                lambda: cof.reputation_multiplier(float("nan")), lambda: cof.reputation_multiplier(float("inf")),
                lambda: cof.reputation_multiplier(101), lambda: cof.reputation_multiplier("5"),
            )
            for work in bad:
                with self.assertRaises(ValueError): work()
            request.assert_not_called()

    def test_clearing_and_preview_use_explicit_commands(self):
        with patch.object(cof.progression, "request", return_value={"ok": True}) as request:
            cof.prophecy("6", 0, None, None, preview=True)
            cof.prophecy("6", 0, 123, 5)
            self.assertEqual([c.args[0] for c in request.call_args_list],
                             ["cofpreview 6 0 none none", "cofprophecy 6 0 123 5"])

    def test_merchant_switch_requires_explicit_flag(self):
        with patch.object(cof.progression, "request", return_value={"ok": True}) as request:
            cof.join("6"); cof.join("6", switch_from_merchant=True)
            self.assertEqual([c.args[0] for c in request.call_args_list], ["cofjoin 6", "cofjoin 6 switch"])

    def test_failure_retains_snapshot(self):
        failed = {"ok": False, "error": "save failed", "backup": "recovery"}
        with patch.object(cof.progression, "request", return_value=failed):
            self.assertEqual(cof.favor("6", 500), failed)
            with self.assertRaisesRegex(RuntimeError, "save failed"): cof.read()

    def test_gain_controls_are_independent_and_old_alias_still_works(self):
        with patch.object(cof.progression, "request", return_value={"ok": True}) as request:
            cof.favor_multiplier(5); cof.reputation_multiplier(2.5); cof.multiplier(1)
            self.assertEqual([c.args[0] for c in request.call_args_list],
                             ["coffavormult 5", "cofrepmult 2.5", "coffavormult 1"])

    def test_tuning_controls_use_separate_explicit_commands(self):
        with patch.object(cof.progression, "request", return_value={"ok": True}) as request:
            cof.charge_multiplier(5); cof.reward_multiplier(2.5)
            cof.double_drop_chance("enemy", 80); cof.double_drop_chance("echo", 0)
            cof.double_drop_chance("enemy", None)
            cof.exalted_multiplier(3); cof.t7_multiplier(4); cof.lp_multiplier(2)
            for lens in ("celerity", "charity", "duplication"): cof.lens_multiplier(lens, 3)
            self.assertEqual([c.args[0] for c in request.call_args_list], [
                "cofchargemult 5", "cofrewardmult 2.5", "cofdouble enemy 80", "cofdouble echo 0",
                "cofdouble enemy reset", "cofexaltedmult 3", "coft7mult 4", "coflpmult 2",
                "coflensmult celerity 3", "coflensmult charity 3", "coflensmult duplication 3"])

    def test_invalid_tuning_values_never_reach_ipc(self):
        with patch.object(cof.progression, "request") as request:
            for method in (cof.charge_multiplier, cof.reward_multiplier, cof.exalted_multiplier, cof.t7_multiplier, cof.lp_multiplier):
                for value in (True, float("nan"), float("inf"), 0, 101, "2"):
                    with self.assertRaises(ValueError): method(value)
            for source, value in (("enemy", True), ("enemy", -1), ("echo", 101), ("enemy", float("nan")), ("both", 50)):
                with self.assertRaises(ValueError): cof.double_drop_chance(source, value)
            for lens, value in (("quality", 2), ("celerity", 0), ("charity", True), ("duplication", float("nan"))):
                with self.assertRaises(ValueError): cof.lens_multiplier(lens, value)
            with self.assertRaises(ValueError): cof.reward_multiplier(25.01)
            request.assert_not_called()


if __name__ == "__main__": unittest.main()
