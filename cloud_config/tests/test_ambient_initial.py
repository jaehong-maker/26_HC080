"""Offline checks for initial ambient commands and subsequent device POLLs."""

import ast
import copy
import importlib
import json
import logging
from pathlib import Path
import re
import sys
import types
import unittest
from datetime import datetime, timedelta, timezone
from unittest.mock import Mock, patch


LAMBDA_DIR = Path(__file__).resolve().parents[1] / "Lambda"


class StateTable:
    def __init__(self):
        self.state = {}

    def get_item(self, **kwargs):
        return {"Item": copy.deepcopy(self.state)}

    def update_item(self, UpdateExpression, ExpressionAttributeValues=None,
                    ConditionExpression=None, **kwargs):
        values = ExpressionAttributeValues or {}
        if ConditionExpression:
            for condition in ConditionExpression.split(" AND "):
                missing = re.fullmatch(r"attribute_not_exists\((\w+)\)", condition)
                if missing:
                    valid = missing[1] not in self.state
                else:
                    field, token = condition.split(" = ")
                    valid = field in self.state and self.state[field] == values[token]
                if not valid:
                    raise RuntimeError("ConditionalCheckFailedException")
        clauses = re.split(r"\b(SET|REMOVE)\b", UpdateExpression, flags=re.I)
        for operation, expression in zip(clauses[1::2], clauses[2::2]):
            for assignment in expression.strip().split(","):
                if operation.upper() == "REMOVE":
                    self.state.pop(assignment.strip(), None)
                else:
                    field, token = assignment.strip().split(" = ")
                    self.state[field] = copy.deepcopy(values[token])


class AmbientInitialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Import real handlers with offline service adapters. Load only the
        # real mailbox function from db_utils to avoid creating AWS clients.
        cls.db = types.ModuleType("db_utils")
        cls.api = types.ModuleType("api_utils")
        cls.module_patch = patch.dict(sys.modules, {"db_utils": cls.db, "api_utils": cls.api})
        cls.module_patch.start()
        sys.path.insert(0, str(LAMBDA_DIR))
        cls.config = importlib.import_module("config")
        cls.ambient = importlib.import_module("handlers.ambient_handler")
        cls.device = importlib.import_module("handlers.device_handler")
        nodes = ast.parse((LAMBDA_DIR / "db_utils.py").read_text(encoding="utf-8-sig"))
        selected = [node for node in nodes.body if isinstance(node, ast.FunctionDef)
                    and node.name in ("manage_mailbox", "get_ambient_scent_recommendation")]
        cls.db.__dict__.update(config=cls.config, logger=logging.getLogger("ambient-tests"))
        exec(compile(ast.Module(body=selected, type_ignores=[]), "db_utils.py", "exec"), cls.db.__dict__)

    @classmethod
    def tearDownClass(cls):
        cls.module_patch.stop()
        sys.path.remove(str(LAMBDA_DIR))

    def setUp(self):
        self.epoch = 1800000000
        self.table = StateTable()
        self.table.state = {"active_mode": "ready", "db_level": 50, "db_avg": 50,
                            "weights": [100] * 4, "current_capacity": 100}
        self.db.state_table = self.table
        self.db.log_table = types.SimpleNamespace(put_item=Mock())
        self.db.get_device_state = lambda device_id: copy.deepcopy(self.table.state)
        self.db.get_saved_region = lambda device_id: ""
        self.db._ambient_scent_ids = lambda: [1, 2, 3, 4, 5, 6]
        self.db._active_kinds = lambda device_id: [1, 2, 3, 4]
        self.db.get_spray_from_kind = lambda device_id, kind: kind
        self.db.get_spray_mapping = lambda device_id: {str(i): i for i in range(1, 5)}
        self.db.apply_due_implicit_like = lambda device_id: None
        self.db.get_music_track = lambda *args, **kwargs: 6

        def update_state(device_id, last_time, capacity, spray, weights):
            self.table.state.update(last_spray_time=last_time, current_capacity=capacity,
                                    last_spray_code=spray)

        self.db.update_device_state = Mock(side_effect=update_state)
        self.api.ask_gemini_ambient_mood = Mock(return_value=(6, {
            "led_r": 255, "led_g": 180, "led_b": 0, "led_bright": 150}))
        self.clock = patch.object(self.ambient, "_get_kst_now", side_effect=lambda:
                                 datetime.fromtimestamp(self.epoch, timezone(timedelta(hours=9))))
        self.poll_clock = patch.object(self.device.time, "time", side_effect=lambda: self.epoch)
        self.clock.start()
        self.poll_clock.start()
        self.addCleanup(self.clock.stop)
        self.addCleanup(self.poll_clock.stop)

    def start(self, **extra):
        response = self.ambient.handle_ambient("ambient", {"action": "noise", **extra}, "SS", [100] * 4)
        self.assertEqual(response["statusCode"], 200, response)
        return json.loads(response["body"])

    def poll(self, level=50):
        response = self.device.handle_device("POLL", "weather", {"db_level": level, "music": 0}, "SS", [100] * 4)
        self.assertEqual(response["statusCode"], 200, response)
        return json.loads(response["body"])

    def test_initial_60_seconds_overrides_legacy_client_duration(self):
        response = self.start(duration=3)
        self.assertEqual((response["spray"], response["duration"]), (2, 60))
        self.assertEqual(self.table.state["pending_duration"], 60)

    def test_initial_command_and_outputs_are_delivered_once_by_poll(self):
        self.start()
        self.epoch += 2
        first = self.poll()
        self.assertEqual((first["spray"], first["duration"], first["app_command"]), (2, 60, True))
        self.assertEqual((first["music"], first["led_r"]), (6, 255))
        self.epoch += 2
        second = self.poll()
        self.assertEqual((second["spray"], second["app_command"], second["music"]), (-1, False, 0))

    def test_entry_resets_old_samples_and_timer_and_bypasses_old_cooldown(self):
        self.table.state.update(db_samples=[{"timestamp": self.epoch - 2, "value": 99}],
                                db_window_ready=True, db_window_started_at=self.epoch - 1000,
                                db_last_sample_time=self.epoch - 2, current_spray=4,
                                last_spray_time=datetime.fromtimestamp(self.epoch, timezone(timedelta(hours=9))).isoformat(),
                                auto_stop_time="2000-01-01T00:00:00+09:00")
        self.assertEqual(self.start()["duration"], 60)
        self.assertEqual(self.table.state["db_samples"], [])
        self.assertFalse(self.table.state["db_window_ready"])
        self.assertNotIn("auto_stop_time", self.table.state)
        self.epoch += 2
        self.assertEqual(self.poll()["spray"], 2)

    def test_duplicate_start_does_not_restart_or_replace_initial_run(self):
        self.start()
        before = copy.deepcopy(self.table.state)
        self.epoch += 2
        response = self.start(db_avg=90, duration=3)
        self.assertEqual((response["spray"], response["app_command"]), (-1, False))
        self.assertEqual(self.table.state, before)
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_measurement_completion_in_same_bucket_does_not_spray(self):
        self.start()
        for elapsed in range(2, 65, 2):
            self.epoch = 1800000000 + elapsed
            response = self.poll()
            if elapsed > 2:
                self.assertEqual(response["spray"], -1)
        self.assertTrue(response["db_window_ready"])
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_bucket_change_waits_for_fresh_window_then_sprays_three_seconds(self):
        self.start()
        for elapsed in range(2, 63, 2):
            self.epoch = 1800000000 + elapsed
            response = self.poll(level=90)
            if 2 < elapsed < 62:
                self.assertEqual(response["spray"], -1)
        self.assertEqual((response["spray"], response["duration"], response["app_command"]), (4, 3, True))
        self.epoch += 2
        self.assertEqual(self.poll(level=90)["spray"], -1)

    def test_stop_during_initial_measurement_is_delivered_by_next_poll(self):
        self.start()
        self.epoch += 2
        self.poll()
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
        self.epoch += 2
        response = self.poll()
        self.assertEqual((response["active_mode"], response["spray"]), ("ready", 0))
        self.epoch += 2
        self.assertEqual(self.poll()["spray"], -1)
        self.assertEqual(self.start()["duration"], 60)

    def test_stop_while_initial_recommendation_is_pending_cancels_start(self):
        def stop_before_reply(*args, **kwargs):
            self.table.state.update(active_mode="ready", pending_cmd=0, last_mode_change_time=self.epoch)
            return 6, {"led_r": 255, "led_g": 180, "led_b": 0, "led_bright": 150}

        self.api.ask_gemini_ambient_mood.side_effect = stop_before_reply
        response = self.start()
        self.assertEqual(response["spray"], -1)
        self.assertEqual(self.table.state["active_mode"], "ready")
        self.assertEqual(self.table.state["pending_cmd"], 0)
        self.assertNotIn("ambient_initial_started_at", self.table.state)

    def test_concurrent_initial_start_cannot_replace_winning_command(self):
        def another_start_wins(*args, **kwargs):
            self.table.state.update(active_mode="ambient", pending_cmd=4, pending_duration=60,
                                    ambient_initial_started_at=self.epoch)
            return 6, {"led_r": 255, "led_g": 180, "led_b": 0, "led_bright": 150}

        self.api.ask_gemini_ambient_mood.side_effect = another_start_wins
        response = self.start()
        self.assertEqual(response["spray"], -1)
        self.assertEqual(self.table.state["pending_cmd"], 4)
        self.assertEqual(self.table.state["pending_duration"], 60)


if __name__ == "__main__":
    unittest.main()
