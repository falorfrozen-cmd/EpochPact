"""IPC boundary validation and recoverable mutation failures."""
import unittest
from unittest.mock import patch
from . import monolith_backend as backend


class MonolithBackendTests(unittest.TestCase):
    def test_open_travels_once_waits_for_rest_and_rechecks_character(self):
        def request(cmd):
            if cmd == "monolithread":
                return next(states)
            if cmd == "identityread":
                return {"ok": True, "player": {"id": "6", "scene": "M_Rest"}}
            if cmd.startswith("monolithpanelready"):
                return {"ok": True, "player": {"id": "6"}, "panelReady": True}
            return {"ok": True}
        states = iter([
            {"ok": True, "editable": True, "player": {"id": "6", "scene": "EoT"}},
            {"ok": True, "restContextReady": False, "player": {"id": "6", "scene": "M_Rest"}},
            {"ok": True, "restContextReady": True, "player": {"id": "6", "scene": "M_Rest"}},
        ])
        with patch.object(backend.progression, "request", side_effect=request) as req, \
             patch.object(backend.progression.le_session, "send", return_value="travel: normal waypoint transition requested for M_Rest") as travel, \
             patch.object(backend.time, "sleep"):
            backend.select("6", 1, False)
            travel.assert_called_once_with("monolithrest 6", timeout=90)
            self.assertEqual(req.call_args_list[-1].args[0], "monolithpanelready 6 1 normal")
            self.assertEqual(sum(c.args[0].startswith("monolithselect") for c in req.call_args_list), 1)

    def test_async_open_is_never_repeated_while_scene_is_loading(self):
        responses = [
            {"ok":True,"editable":True,"restContextReady":True,"player":{"id":"6","scene":"M_Rest"}},
            {"ok":True},
            {"ok":False,"error":"scene transitioning"},
            {"ok":True,"panelReady":False,"player":{"id":"6"}},
            {"ok":True,"panelReady":True,"player":{"id":"6"}},
            {"ok":True,"panelReady":True,"player":{"id":"6"}},
        ]
        with patch.object(backend.progression,"request",side_effect=responses) as req, patch.object(backend.time,"sleep"):
            self.assertTrue(backend.select("6",1,False)["ok"])
        self.assertEqual(sum(c.args[0].startswith("monolithselect") for c in req.call_args_list),1)

    def test_character_change_during_panel_wait_refuses_without_retry(self):
        with patch.object(backend.progression,"request",side_effect=[
            {"ok":True,"editable":True,"restContextReady":True,"player":{"id":"6","scene":"M_Rest"}},
            {"ok":True}, {"ok":True,"panelReady":True,"player":{"id":"0"}},
        ]) as req:
            with self.assertRaisesRegex(RuntimeError,"character changed"):
                backend.select("6",1,False)
        self.assertEqual(sum(c.args[0].startswith("monolithselect") for c in req.call_args_list),1)

    def test_wrong_character_or_echo_never_travels_or_opens(self):
        for live in (
            {"ok": True, "editable": True, "player": {"id": "0", "scene": "EoT"}},
            {"ok": True, "editable": False, "player": {"id": "6", "scene": "M_D20"}},
        ):
            with patch.object(backend.progression, "request", return_value=live) as req, \
                 patch.object(backend.progression.le_session, "send") as travel:
                with self.assertRaises(RuntimeError):
                    backend.focus_echo("6", 1, False, 2)
                travel.assert_not_called()
                self.assertEqual([c.args[0] for c in req.call_args_list], ["monolithread"])

    def test_reading_echoes_never_travels(self):
        with patch.object(backend.progression, "request", return_value={"ok": True}) as req, \
             patch.object(backend.progression.le_session, "send") as travel:
            backend.echoes("6", 1, False)
            req.assert_called_once_with("echoread 6 1 normal")
            travel.assert_not_called()

    def test_invalid_inputs_never_reach_ipc(self):
        with patch.object(backend.progression, "request") as request:
            for save_id in ("", "6\nquestscomplete 0", "a", 6):
                with self.assertRaises(ValueError):
                    backend.unlock(save_id)
            for timeline, empowered, value in (
                (True, True, 300), (0, True, 300), (7, 1, 300),
                (7, True, True), (7, True, -1), (7, True, 1.5),
            ):
                with self.assertRaises(ValueError):
                    backend.corruption("6", timeline, empowered, value)
            for value in (True, float("nan"), float("inf"), 0, 101, "5\nstat strength 80"):
                with self.assertRaises(ValueError):
                    backend.multiplier(value)
            request.assert_not_called()

    def test_normal_and_empowered_are_distinct(self):
        with patch.object(backend.progression, "request", return_value={"ok": True}) as request:
            backend.corruption("6", 7, False, 30)
            backend.corruption("6", 7, True, 300)
            self.assertEqual([call.args[0] for call in request.call_args_list],
                             ["corruption 6 7 normal 30", "corruption 6 7 empowered 300"])

    def test_read_failure_is_visible(self):
        with patch.object(backend.progression, "request", return_value={"ok": False, "error": "wrong character"}):
            with self.assertRaisesRegex(RuntimeError, "wrong character"):
                backend.read()

    def test_mutation_failure_preserves_recovery_path(self):
        failure = {"ok": False, "error": "save failed", "backup": "snapshot"}
        with patch.object(backend.progression, "request", return_value=failure):
            self.assertEqual(backend.stability("6", 7, True, 300), failure)


if __name__ == "__main__":
    unittest.main()
