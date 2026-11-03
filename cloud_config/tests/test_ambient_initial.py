"""Offline checks for initial ambient commands and subsequent device POLLs."""

import ast
import copy
import importlib
import json
import logging
import math
from pathlib import Path
import re
import sys
import types
import unittest
import uuid
from datetime import datetime, timedelta, timezone
from unittest.mock import MagicMock, Mock, patch


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
        cls.feedback = importlib.import_module("handlers.feedback_handler")
        nodes = ast.parse((LAMBDA_DIR / "db_utils.py").read_text(encoding="utf-8-sig"))
        selected = [node for node in nodes.body if isinstance(node, ast.FunctionDef)
                    and node.name in ("manage_mailbox", "get_ambient_scent_recommendation", "get_ambient_spray_recommendation", "renew_ambient_lease", "select_ambient_music", "rotate_ambient_music")]
        cls.db.__dict__.update(config=cls.config, uuid=uuid, math=math, logger=logging.getLogger("ambient-tests"))
        exec(compile(ast.Module(body=selected, type_ignores=[]), "db_utils.py", "exec"), cls.db.__dict__)

    @classmethod
    def tearDownClass(cls):
        cls.module_patch.stop()
        sys.path.remove(str(LAMBDA_DIR))

    def setUp(self):
        self.epoch = 1800000000
        self.playing_music = 0
        self.pump_expires_at = 0
        self.spray_commands = []
        self.sensor_weights = [100] * 4
        self.table = StateTable()
        self.table.state = {"active_mode": "ready", "db_level": 50, "db_avg": 50,
                            "weights": [100] * 4, "current_capacity": 100}
        self.db.state_table = self.table
        self.db.log_table = types.SimpleNamespace(put_item=Mock())
        self.db.get_device_state = lambda device_id: copy.deepcopy(self.table.state)
        self.db._get_kst_now = lambda: datetime.fromtimestamp(self.epoch, timezone(timedelta(hours=9)))
        self.db.get_saved_region = lambda device_id: ""
        self.db._ambient_scent_ids = lambda: [1, 2, 3, 4, 5, 6]
        self.db._active_kinds = lambda device_id: [1, 2, 3, 4]
        self.db.get_spray_from_kind = lambda device_id, kind: kind
        self.db.get_spray_mapping = lambda device_id: {str(i): i for i in range(1, 5)}
        self.db.apply_due_implicit_like = lambda device_id: None
        self.db.get_music_track = lambda *args, **kwargs: 6
        self.db.cancel_implicit_like = Mock()
        self.db.seed_ambient_defaults = Mock()
        self.db.save_ambient_feedback = Mock(return_value=True)
        self.db.get_kind_from_spray = lambda device_id, spray: spray
        self.db.get_ambient_alternative = Mock(return_value=4)
        self.db.schedule_implicit_like = Mock(return_value=True)

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

    def poll(self, level=50, microphone_valid=True, include_db=True, music_finished=None):
        payload = {"music": self.playing_music}
        if music_finished is not None:
            payload["music_finished"] = music_finished
        if microphone_valid is not None:
            payload["microphone_valid"] = microphone_valid
        if include_db:
            payload["db_level"] = level
        response = self.device.handle_device("POLL", "weather", payload, "SS", self.sensor_weights)
        self.assertEqual(response["statusCode"], 200, response)
        body = json.loads(response["body"])
        if body["active_mode"] in ("ready", "off"):
            self.playing_music = 0
            self.pump_expires_at = self.epoch
        elif body["spray"] == 0:
            self.pump_expires_at = self.epoch
        if body["music"] > 0:
            self.playing_music = body["music"]
        if body["app_command"] and body["spray"] > 0:
            self.pump_expires_at = self.epoch + body["duration"]
            self.spray_commands.append((self.epoch, body["spray"], body["duration"]))
        return body

    def finish_probe(self, levels=(50, 50, 50)):
        self.epoch += 2
        self.assertEqual(self.poll(level=99)["spray"], 0)  # pre-stop snapshot is discarded
        for level in levels:
            self.epoch += 2
            response = self.poll(level)
        return response

    def start_selected(self, level=50, **extra):
        self.assertEqual(self.start(**extra)["spray"], -1)
        return self.finish_probe((level, level, level))

    def test_start_waits_for_fresh_samples_and_ignores_stored_average(self):
        self.table.state.update(db_avg=99, db_level=99)
        response = self.start()
        self.assertTrue(response["ambient_startup_pending"])
        self.assertEqual(response["spray"], -1)
        self.api.ask_gemini_ambient_mood.assert_not_called()
        self.epoch += 2
        self.assertEqual(self.poll(99)["spray"], 0)
        for level in (40, 40):
            self.epoch += 2
            self.assertEqual(self.poll(level)["spray"], -1)
            self.api.ask_gemini_ambient_mood.assert_not_called()
        self.epoch += 2
        response = self.poll(100)
        self.assertEqual(self.table.state["ambient_startup_avg"], 60)
        self.assertEqual((response["spray"], response["duration"]), (2, 60))
        self.assertIn("Moderate", response["ambient_context"])
        self.assertEqual(self.api.ask_gemini_ambient_mood.call_args.args[0], 60)

    def test_initial_60_seconds_overrides_legacy_client_duration(self):
        response = self.start_selected(duration=3)
        self.assertEqual((response["spray"], response["duration"]), (2, 60))

    def test_initial_command_and_outputs_are_delivered_once_by_poll(self):
        first = self.start_selected()
        self.assertEqual((first["spray"], first["duration"], first["app_command"]), (2, 60, True))
        self.assertEqual((first["music"], first["led_r"]), (6, 255))
        self.epoch += 2
        second = self.poll()
        self.assertEqual((second["spray"], second["app_command"], second["music"]), (-1, False, 0))

    def test_entry_resets_old_samples_and_timer(self):
        self.table.state.update(db_samples=[{"timestamp": self.epoch - 2, "value": 99}],
                                db_window_ready=True, db_window_started_at=self.epoch - 1000,
                                db_last_sample_time=self.epoch - 2, current_spray=4,
                                last_spray_time=datetime.fromtimestamp(self.epoch, timezone(timedelta(hours=9))).isoformat(),
                                auto_stop_time="2000-01-01T00:00:00+09:00")
        self.assertEqual(self.start()["spray"], -1)
        self.assertEqual(self.table.state["db_samples"], [])
        self.assertFalse(self.table.state["db_window_ready"])
        self.assertNotIn("auto_stop_time", self.table.state)
        self.assertEqual(self.finish_probe()["duration"], 60)

    def test_duplicate_start_does_not_restart_or_replace_initial_run(self):
        self.start()
        before = copy.deepcopy(self.table.state)
        self.epoch += 2
        response = self.start(db_avg=90, duration=3)
        self.assertEqual((response["spray"], response["app_command"]), (-1, False))
        self.assertEqual(self.table.state, before)
        self.api.ask_gemini_ambient_mood.assert_not_called()

    def test_duplicate_start_during_sixty_second_spray_does_not_restart(self):
        self.start_selected()
        before = copy.deepcopy(self.table.state)
        self.assertEqual(self.start(db_avg=90)["spray"], -1)
        self.assertEqual(self.table.state, before)
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_same_bucket_keeps_running_across_sixty_second_boundaries(self):
        self.start_selected()
        base_epoch = self.epoch
        for elapsed in range(2, 185, 2):
            self.epoch = base_epoch + elapsed
            self.assertGreater(self.pump_expires_at, self.epoch)
            response = self.poll()
            if elapsed % 30 == 0:
                self.assertEqual((response["spray"], response["duration"]), (2, 60))
            else:
                self.assertEqual(response["spray"], -1)
        self.assertTrue(response["db_window_ready"])
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_same_bucket_rotates_only_music_each_minute_and_keeps_dispensing(self):
        self.start_selected()
        initial_scent = self.table.state["active_scent"]
        initial_led = tuple(self.table.state[field] for field in ("led_r", "led_g", "led_b", "led_br"))
        start = self.epoch
        changes = []
        for elapsed in range(2, 185, 2):
            self.epoch = start + elapsed + 0.25  # production timestamps have fractions
            self.assertGreater(self.pump_expires_at, self.epoch)
            response = self.poll(45 if elapsed % 4 == 0 else 55)
            if response["music"] > 0:
                changes.append((elapsed, response["music"]))
                # The independent 30-second lease renewal can share this POLL.
                self.assertEqual(response["spray"], initial_scent)
            self.assertEqual(response["active_scent"], initial_scent)
            self.assertEqual(tuple(response[field] for field in ("led_r", "led_g", "led_b", "led_br")), initial_led)
        self.assertEqual(changes, [(60, 7), (120, 8), (180, 9)])
        self.assertTrue(all(scent == initial_scent for _, scent, _ in self.spray_commands))
        self.assertEqual([int(when - start) for when, _, _ in self.spray_commands[1:]], list(range(30, 181, 30)))
        self.api.ask_gemini_ambient_mood.assert_called_once()
        self.db.update_device_state.assert_called_once()  # music does not consume scent

    def test_same_bucket_plays_the_whole_song_then_advances_without_changing_outputs(self):
        self.start_selected(level=70)
        self.assertEqual(self.playing_music, 3)  # AI's Quiet track 6 is rejected for Moderate.
        scent = self.table.state["active_scent"]
        led = tuple(self.table.state[field] for field in ("led_r", "led_g", "led_b", "led_br"))
        start = self.epoch
        for elapsed in range(2, 185, 2):
            self.epoch = start + elapsed
            response = self.poll(70, music_finished=False)
            self.assertEqual(response["music"], 0)  # No 60-second interruption.
            self.assertEqual(self.playing_music, 3)
            self.assertGreater(self.pump_expires_at, self.epoch)
        self.epoch += 2
        response = self.poll(70, music_finished=True)
        self.assertEqual((response["music"], response["spray"], response["app_command"]), (4, -1, False))
        self.assertEqual(self.table.state["active_scent"], scent)
        self.assertEqual(tuple(self.table.state[field] for field in ("led_r", "led_g", "led_b", "led_br")), led)
        self.assertTrue(all(command[1] == scent for command in self.spray_commands))
        self.assertEqual(self.poll(70, music_finished=False)["music"], 0)
        self.api.ask_gemini_ambient_mood.assert_called_once()
        self.db.update_device_state.assert_called_once()

    def test_finished_short_song_advances_before_a_minute_without_a_noise_window(self):
        self.start_selected(level=70)
        self.epoch += 2
        self.assertFalse(self.table.state["db_window_ready"])
        response = self.poll(70, music_finished=True)
        self.assertEqual((response["music"], response["spray"]), (4, -1))

    def test_finished_song_advances_even_if_microphone_is_temporarily_invalid(self):
        self.start_selected()
        self.epoch += 2
        response = self.poll(microphone_valid=False, include_db=False, music_finished=True)
        self.assertEqual((response["music"], response["spray"]), (7, -1))
        self.assertFalse(response["microphone_valid"])

    def test_repeated_finish_report_retries_the_same_next_song_without_skipping(self):
        self.start_selected(level=70)
        self.epoch += 2
        self.assertEqual(self.poll(70, music_finished=True)["music"], 4)
        self.playing_music = 3  # Simulate losing the next-song response.
        self.epoch += 2
        self.assertEqual(self.poll(70, music_finished=True)["music"], 4)
        self.epoch += 2
        self.assertEqual(self.poll(70, music_finished=False)["music"], 0)
        self.epoch += 2
        self.assertEqual(self.poll(70, music_finished=True)["music"], 10)

    def test_finished_track_must_match_current_selection_and_be_nonzero(self):
        self.start_selected(level=70)
        before = copy.deepcopy(self.table.state)
        for track in (None, 0, 6, 4):
            with self.subTest(track=track):
                self.assertFalse(self.db.rotate_ambient_music("SS", self.epoch, track, True))
                self.assertEqual(self.table.state, before)

    def test_app_cannot_advance_music_by_reporting_a_finished_song(self):
        self.start_selected(level=70)
        before = copy.deepcopy(self.table.state)
        response = self.device.handle_device("POLL", "ambient", {
            "email": "user@example.com", "music": 3, "music_finished": True,
        }, "SS", [100] * 4)
        self.assertEqual(json.loads(response["body"])["music"], 3)
        self.assertEqual(self.table.state, before)

    def test_finished_report_after_stop_does_not_resume_music(self):
        self.start_selected(level=70)
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
        response = self.poll(70, music_finished=True)
        self.assertEqual((response["spray"], response["music"], response["active_mode"]), (0, 0, "ready"))

    def test_selection_validates_ai_tracks_and_falls_back_inside_each_bucket(self):
        for bucket, tracks in self.config.AMBIENT_MUSIC_BY_BUCKET.items():
            with self.subTest(bucket=bucket):
                for suggestion in (0, 6, 26, -1, 999):
                    selected = self.db.select_ambient_music(bucket, suggestion)
                    self.assertIn(selected, tracks)
                self.assertEqual(self.db.select_ambient_music(bucket, tracks[-1]), tracks[-1])
                self.assertEqual(self.db.select_ambient_music(bucket, tracks[0], tracks[0]), tracks[1])
                self.assertEqual(self.db.select_ambient_music(bucket, current_music=tracks[-1]), tracks[0])

    def test_moderate_initial_ai_failure_still_starts_a_moderate_song(self):
        self.api.ask_gemini_ambient_mood.return_value = (0, {})
        response = self.start_selected(level=70)
        self.assertEqual(response["music"], 3)
        self.assertIn("Moderate", response["ambient_context"])

    def test_same_ai_suggestion_cannot_keep_moderate_on_forest(self):
        self.start_selected(level=70)
        self.assertEqual(self.playing_music, 3)
        for expected in (4, 10, 12, 13, 14, 16, 20, 3):
            self.epoch += 2
            response = self.poll(70, music_finished=True)
            self.assertEqual(response["music"], expected)
            self.assertNotEqual(response["music"], 6)
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_moderate_ai_prompt_uses_actual_allowed_titles_and_example(self):
        request = Mock()
        response = MagicMock()
        response.__enter__.return_value.read.return_value = json.dumps({
            "candidates": [{"content": {"parts": [{"text": json.dumps({"music": 3})}]}}],
        }).encode("utf-8")
        network = types.SimpleNamespace(Request=Mock(return_value=request), urlopen=Mock(return_value=response))
        namespace = {
            "config": self.config, "os": types.SimpleNamespace(environ={}), "json": json,
            "logger": logging.getLogger("ambient-tests"), "urllib": types.SimpleNamespace(request=network),
        }
        source = ast.parse((LAMBDA_DIR / "api_utils.py").read_text(encoding="utf-8-sig"))
        function = next(node for node in source.body if isinstance(node, ast.FunctionDef)
                        and node.name == "ask_gemini_ambient_mood")
        exec(compile(ast.Module(body=[function], type_ignores=[]), "api_utils.py", "exec"), namespace)
        with patch.object(self.config, "GEMINI_API_KEY", "offline-test", create=True):
            music, _ = namespace["ask_gemini_ambient_mood"](70, 70, False, 0)
        self.assertEqual(music, 3)
        payload = json.loads(network.Request.call_args.kwargs["data"])
        prompt = payload["contents"][0]["parts"][0]["text"]
        self.assertIn("Moderate", prompt)
        self.assertIn("3: 바이,썸머", prompt)
        self.assertNotIn("6: 숲", prompt)
        self.assertIn('"music": 3', prompt)

    def test_stop_wins_while_finished_song_is_selecting_the_next_track(self):
        self.start_selected(level=70)
        original_update = self.table.update_item

        def stop_before_rotation(**kwargs):
            if kwargs["UpdateExpression"].startswith("set current_music = :music, ambient_music_changed_at"):
                self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
            return original_update(**kwargs)

        self.table.update_item = stop_before_rotation
        self.epoch += 2
        response = self.poll(70, music_finished=True)
        self.assertEqual((response["spray"], response["music"], response["active_mode"]), (0, 0, "ready"))

    def test_bucket_change_takes_priority_over_finished_song(self):
        self.start_selected()
        start = self.epoch
        for elapsed in range(2, 61, 2):
            self.epoch = start + elapsed
            self.poll(90, music_finished=False)
        self.epoch = start + 62
        with patch.object(self.db, "rotate_ambient_music", wraps=self.db.rotate_ambient_music) as rotate:
            response = self.poll(90, music_finished=True)
            rotate.assert_not_called()
        self.assertEqual((response["spray"], response["music"]), (4, 1))
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_rotation_uses_each_bucket_playlist_and_excludes_the_current_track(self):
        for bucket, level in (("Quiet", 50), ("Moderate", 70), ("Loud", 90)):
            with self.subTest(bucket=bucket):
                tracks = self.config.AMBIENT_MUSIC_BY_BUCKET[bucket]
                self.table.state.update(
                    active_mode="ambient", ambient_context="Night_" + bucket, active_scent=2,
                    ambient_renewal_enabled=True, ambient_startup_pending=False, pending_cmd=-1,
                    current_music=tracks[-1], ambient_music_changed_at=self.epoch - 60,
                    microphone_valid=True, db_last_sample_time=self.epoch, db_window_ready=True, db_avg=level,
                )
                before = copy.deepcopy(self.table.state)
                self.assertTrue(self.db.rotate_ambient_music("SS", self.epoch, tracks[-1]))
                self.assertEqual(self.table.state["current_music"], tracks[0])
                for field, value in before.items():
                    if field not in ("current_music", "ambient_music_changed_at"):
                        self.assertEqual(self.table.state[field], value, field)
                self.assertFalse(self.db.rotate_ambient_music("SS", self.epoch, tracks[0]))

    def test_app_peek_does_not_rotate_or_consume_due_music(self):
        self.start_selected()
        start = self.epoch
        for elapsed in range(2, 60, 2):
            self.epoch = start + elapsed
            self.poll()
        self.epoch = start + 60
        before = copy.deepcopy(self.table.state)
        response = self.device.handle_device("POLL", "ambient", {"email": "user@example.com"}, "SS", [100] * 4)
        self.assertEqual(json.loads(response["body"])["music"], 6)
        self.assertEqual(self.table.state, before)
        response = self.poll()
        self.assertEqual((response["music"], response["spray"]), (7, 2))
        self.assertEqual(self.poll()["music"], 0)

    def test_due_rotation_continues_during_invalid_measurement(self):
        self.start_selected()
        start = self.epoch
        for elapsed in range(2, 60, 2):
            self.epoch = start + elapsed
            self.poll()
        self.epoch = start + 60
        self.assertEqual(self.poll(microphone_valid=False, include_db=False)["music"], 7)
        self.assertEqual(self.table.state["current_music"], 7)
        self.epoch += 2
        self.assertEqual(self.poll()["music"], 0)

    def test_legacy_moderate_music_keeps_rotating_without_a_valid_noise_window(self):
        self.start_selected(level=70)
        start = self.epoch
        changes = []
        for elapsed in range(2, 183, 2):
            self.epoch = start + elapsed
            response = self.poll(microphone_valid=False, include_db=False)
            self.assertFalse(response["db_window_ready"])
            self.assertIn("Moderate", response["ambient_context"])
            self.assertEqual(response["active_scent"], 2)
            self.assertGreater(self.pump_expires_at, self.epoch)
            if response["music"] > 0:
                changes.append((elapsed, response["music"]))
        self.assertEqual(changes, [(60, 4), (120, 10), (180, 12)])
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_music_rotation_is_not_cancelled_by_an_unrelated_noise_update(self):
        self.start_selected()
        self.epoch += 60
        original_update = self.table.update_item

        def change_noise_before_rotation(**kwargs):
            if kwargs["UpdateExpression"].startswith("set current_music = :music, ambient_music_changed_at"):
                self.table.state.update(microphone_valid=False, db_window_ready=False, db_avg=90)
            return original_update(**kwargs)

        self.table.update_item = change_noise_before_rotation
        self.assertTrue(self.db.rotate_ambient_music("SS", self.epoch, reported_music=6))
        self.assertEqual(self.table.state["current_music"], 7)
        self.assertFalse(self.table.state["microphone_valid"])
        self.assertEqual(self.table.state["db_avg"], 90)

    def test_diagnostics_distinguish_the_selected_track_from_device_acknowledgement(self):
        first = self.start_selected()
        self.assertEqual(first["ambient_music_policy"], "finished-or-60s-v2")
        self.assertEqual(first["ambient_music_rotation_seconds"], 60)
        self.assertEqual(first["device_reported_music"], 0)
        self.assertFalse(first["device_music_finish_reporting"])
        start = self.epoch
        for elapsed in range(2, 61, 2):
            self.epoch = start + elapsed
            response = self.poll()
        self.assertEqual((response["music"], response["device_reported_music"]), (7, 6))
        self.assertEqual(response["ambient_music_changed_at"], self.epoch)
        self.assertEqual(response["device_last_poll_time"], self.epoch)
        self.epoch += 2
        acknowledged = self.poll(music_finished=False)
        self.assertEqual((acknowledged["music"], acknowledged["device_reported_music"]), (0, 7))
        self.assertTrue(acknowledged["device_music_finish_reporting"])

    def test_long_sample_gap_does_not_block_music_rotation(self):
        self.start_selected()
        start = self.epoch
        for elapsed in range(2, 60, 2):
            self.epoch = start + elapsed
            self.poll()
        self.epoch += 10
        reset_at = self.epoch
        response = self.poll()
        self.assertEqual(response["music"], 7)
        self.assertFalse(response["db_window_ready"])
        for elapsed in range(2, 61, 2):
            self.epoch = reset_at + elapsed
            response = self.poll()
            self.assertEqual(response["music"], 8 if elapsed == 60 else 0)

    def test_rotation_does_not_replace_a_music_command_not_yet_received_by_hardware(self):
        self.start_selected()
        self.table.state.update(db_window_ready=True, db_avg=50, ambient_music_changed_at=self.epoch - 60)
        before = copy.deepcopy(self.table.state)
        self.assertFalse(self.db.rotate_ambient_music("SS", self.epoch, reported_music=0))
        self.assertEqual(self.table.state, before)

    def test_music_rotation_rejected_when_stop_wins_before_state_write(self):
        self.start_selected()
        self.table.state.update(db_window_ready=True, db_avg=50, ambient_music_changed_at=self.epoch - 60)
        original_update = self.table.update_item

        def stop_before_rotation(**kwargs):
            if kwargs["UpdateExpression"].startswith("set current_music = :music, ambient_music_changed_at"):
                self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
            return original_update(**kwargs)

        self.table.update_item = stop_before_rotation
        self.assertFalse(self.db.rotate_ambient_music("SS", self.epoch, 6))
        self.assertEqual(self.table.state["active_mode"], "ready")
        self.assertEqual((self.table.state["pending_cmd"], self.table.state["current_music"]), (0, 6))
        response = self.poll()
        self.assertEqual((response["spray"], response["music"]), (0, 0))

    def test_bucket_change_takes_priority_over_due_music_rotation(self):
        self.start_selected()
        start = self.epoch
        with patch.object(self.db, "rotate_ambient_music", wraps=self.db.rotate_ambient_music) as rotate:
            for elapsed in range(2, 61, 2):
                self.epoch = start + elapsed
                self.poll(90)
            rotate.reset_mock()
            self.epoch = start + 62
            response = self.poll(90)
            rotate.assert_not_called()
        self.assertEqual((response["spray"], response["music"]), (4, 1))
        self.api.ask_gemini_ambient_mood.assert_called_once()
        self.assertEqual(self.table.state["ambient_music_changed_at"], self.epoch)

    def test_music_only_response_is_retried_until_the_device_reports_the_track(self):
        self.start_selected()
        start = self.epoch
        for elapsed in range(2, 61, 2):
            self.epoch = start + elapsed
            response = self.poll()
        self.assertEqual(response["music"], 7)
        self.playing_music = 6  # response was lost before reaching the player
        self.epoch += 2
        self.assertEqual(self.poll()["music"], 7)
        self.epoch += 2
        self.assertEqual(self.poll()["music"], 0)

    def test_bucket_change_replaces_scent_with_a_sixty_second_command(self):
        self.start_selected()
        base_epoch = self.epoch
        for elapsed in range(2, 63, 2):
            self.epoch = base_epoch + elapsed
            response = self.poll(level=90)
            if elapsed < 62 and elapsed % 30 != 0:
                self.assertEqual(response["spray"], -1)
        self.assertEqual((response["spray"], response["duration"], response["app_command"]), (4, 60, True))
        self.epoch += 2
        self.assertEqual(self.poll(level=90)["spray"], -1)

    def test_stop_during_initial_measurement_is_delivered_by_next_poll(self):
        self.start_selected()
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
        self.epoch += 2
        response = self.poll()
        self.assertEqual((response["active_mode"], response["spray"]), ("ready", 0))
        self.epoch += 2
        self.assertEqual(self.poll()["spray"], -1)
        self.assertEqual(self.start_selected()["duration"], 60)

    def test_stop_while_initial_recommendation_is_pending_cancels_start(self):
        def stop_before_reply(*args, **kwargs):
            self.table.state.update(active_mode="ready", pending_cmd=0, last_mode_change_time=self.epoch)
            return 6, {"led_r": 255, "led_g": 180, "led_b": 0, "led_bright": 150}

        self.api.ask_gemini_ambient_mood.side_effect = stop_before_reply
        self.start()
        response = self.finish_probe()
        self.assertEqual(response["spray"], 0)
        self.assertEqual(self.table.state["active_mode"], "ready")
        self.assertEqual(self.table.state["pending_cmd"], -1)
        self.assertNotIn("ambient_initial_started_at", self.table.state)

    def test_concurrent_initial_start_cannot_replace_winning_command(self):
        def another_start_wins(*args, **kwargs):
            self.table.state.update(active_mode="ambient", pending_cmd=4, pending_duration=60,
                                    ambient_initial_started_at=self.epoch, ambient_startup_pending=False)
            return 6, {"led_r": 255, "led_g": 180, "led_b": 0, "led_bright": 150}

        self.api.ask_gemini_ambient_mood.side_effect = another_start_wins
        self.start()
        response = self.finish_probe()
        self.assertEqual((response["spray"], response["duration"]), (4, 60))
        self.assertEqual(self.table.state["ambient_initial_started_at"], self.epoch)

    def test_missing_microphone_values_do_not_select_a_default_scent(self):
        self.start()
        self.epoch += 2
        self.poll()
        for level in (None, "bad", float("nan")):
            self.epoch += 2
            self.assertEqual(self.poll(level)["spray"], -1)
        self.assertTrue(self.table.state["ambient_startup_pending"])
        self.api.ask_gemini_ambient_mood.assert_not_called()

    def test_valid_zero_can_select_the_initial_scent(self):
        response = self.start_selected(level=0)
        self.assertEqual(self.table.state["ambient_startup_avg"], 0)
        self.assertEqual(self.api.ask_gemini_ambient_mood.call_args.args[0], 0)
        self.assertEqual((response["spray"], response["duration"]), (2, 60))
        self.assertTrue(response["microphone_valid"])
        self.assertEqual(response["db_level"], 0)
        self.assertEqual(response["db_last_sample_time"], self.epoch)

    def test_valid_zero_is_included_in_the_rolling_average(self):
        self.start_selected()
        self.epoch += 2
        response = self.poll(0)
        self.assertTrue(response["microphone_valid"])
        self.assertEqual((response["db_level"], response["db_avg"]), (0, 0))
        self.epoch += 2
        response = self.poll(60)
        self.assertEqual(response["db_avg"], 30)
        self.assertEqual([sample["value"] for sample in self.table.state["db_samples"]], [0, 60])

    def test_invalid_payloads_do_not_change_samples_average_or_last_valid_time(self):
        self.start_selected()
        self.epoch += 2
        self.poll(50)
        before = copy.deepcopy(self.table.state)
        payloads = [
            (0, False, True), (90, False, True), (50, False, False),
            (50, True, False), (50, None, True), (None, True, True),
            (float("nan"), True, True), (float("inf"), True, True),
            (True, True, True), (-1, True, True), ("bad", True, True),
        ]
        for level, valid, include_db in payloads:
            with self.subTest(level=level, valid=valid, include_db=include_db):
                self.epoch += 0.25
                response = self.poll(level, microphone_valid=valid, include_db=include_db)
                self.assertFalse(response["microphone_valid"])
                self.assertNotIn("db_level", response)
                for field in ("db_samples", "db_level", "db_avg", "db_last_sample_time", "ambient_context"):
                    self.assertEqual(self.table.state[field], before[field], field)
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_invalid_polls_reset_analysis_at_exactly_ten_seconds_without_valid_samples(self):
        self.poll(80)
        last_valid = self.epoch
        self.epoch = last_valid + 9
        self.poll(microphone_valid=False, include_db=False)
        self.assertEqual(len(self.table.state["db_samples"]), 1)
        self.epoch = last_valid + 10
        response = self.poll(microphone_valid=False, include_db=False)
        self.assertEqual(self.table.state["db_samples"], [])
        self.assertEqual(self.table.state["db_last_sample_time"], last_valid)
        self.assertFalse(response["db_window_ready"])
        self.assertNotIn("db_avg", response)
        self.assertNotIn("db_stddev", response)
        self.epoch += 1
        response = self.poll(0)
        self.assertEqual((response["db_level"], response["db_avg"]), (0, 0))
        self.assertEqual(self.table.state["db_window_started_at"], self.epoch)

    def test_reconnection_at_ten_seconds_restarts_the_window(self):
        self.start_selected()
        self.epoch += 2
        self.poll(50)
        self.epoch += 10
        response = self.poll(90)
        self.assertEqual([sample["value"] for sample in self.table.state["db_samples"]], [90])
        self.assertEqual(response["db_avg"], 90)
        self.assertFalse(response["db_window_ready"])
        self.assertFalse(response["ambient_sustained_loud"])
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_invalid_gap_restarts_startup_instead_of_using_old_probe_samples(self):
        self.start()
        self.epoch += 2
        self.poll()  # stop delivery
        self.epoch += 2
        self.poll(90)
        last_valid = self.epoch
        for elapsed in (2, 4, 6, 8, 10):
            self.epoch = last_valid + elapsed
            self.poll(microphone_valid=False, include_db=False)
        self.assertEqual(self.table.state["ambient_startup_samples"], [])
        self.assertEqual(self.table.state["db_last_sample_time"], last_valid)
        self.assertNotIn("ambient_startup_avg", self.table.state)
        self.api.ask_gemini_ambient_mood.assert_not_called()
        for elapsed in (11, 13, 15):
            self.epoch = last_valid + elapsed
            response = self.poll(0)
            if elapsed < 15:
                self.assertEqual(response["spray"], -1)
        self.assertEqual((response["spray"], self.table.state["ambient_startup_avg"]), (2, 0))

    def test_an_invalid_poll_breaks_the_continuous_noise_timer(self):
        self.start_selected(level=90)
        start = self.epoch
        for elapsed in range(2, 63, 2):
            self.epoch = start + elapsed
            response = self.poll(90)
        self.assertTrue(response["ambient_sustained_loud"])
        self.epoch += 2
        self.poll(microphone_valid=False, include_db=False)
        self.assertEqual(self.table.state["ambient_loud_since"], 0)
        self.assertFalse(self.table.state["ambient_sustained_loud"])
        self.epoch += 2
        recovered = self.epoch
        self.poll(90)
        self.assertEqual(self.table.state["ambient_loud_since"], recovered)
        for elapsed in range(2, 61, 2):
            self.epoch = recovered + elapsed
            response = self.poll(90)
            self.assertEqual(response["ambient_sustained_loud"], elapsed == 60)

    def test_invalid_microphone_still_updates_weights_and_delivers_due_renewal(self):
        self.start_selected()
        last_valid = self.table.state["db_last_sample_time"]
        self.sensor_weights = [81, 82, 83, 84]
        self.epoch += 30
        response = self.poll(microphone_valid=False, include_db=False)
        self.assertEqual(response["weights"], self.sensor_weights)
        self.assertEqual((response["spray"], response["duration"], response["app_command"]), (2, 60, True))
        self.assertEqual(self.table.state["db_last_sample_time"], last_valid)
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_invalid_microphone_still_delivers_pending_manual_command(self):
        self.device.handle_device("MANUAL", "manual", {"spray": 3, "duration": 8}, "SS", [100] * 4)
        response = self.poll(microphone_valid=False, include_db=False)
        self.assertEqual((response["spray"], response["duration"], response["app_command"]), (3, 8, True))
        self.assertEqual(self.poll(microphone_valid=False, include_db=False)["spray"], -1)

    def test_stop_wins_over_pending_renewal_even_with_invalid_microphone(self):
        self.start_selected()
        self.epoch += 30
        self.assertTrue(self.db.renew_ambient_lease("SS", self.epoch))
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
        response = self.poll(microphone_valid=False, include_db=False)
        self.assertEqual((response["active_mode"], response["spray"], response["app_command"]), ("ready", 0, True))
        self.epoch += 60
        self.assertEqual(self.poll(microphone_valid=False, include_db=False)["spray"], -1)
        self.assertFalse(self.table.state["ambient_renewal_enabled"])

    def test_invalid_poll_cannot_trigger_a_recommendation_without_previous_context(self):
        self.table.state.update(active_mode="ambient", ambient_context="")
        self.poll(microphone_valid=False, include_db=False)
        self.api.ask_gemini_ambient_mood.assert_not_called()

    def test_unverified_legacy_samples_are_discarded_once(self):
        self.table.state.update(db_samples=[{"timestamp": self.epoch - 2, "value": 0}],
                                db_last_sample_time=self.epoch - 2, db_avg=0)
        last_valid = self.table.state["db_last_sample_time"]
        response = self.poll(microphone_valid=False, include_db=False)
        self.assertEqual(self.table.state["db_samples"], [])
        self.assertNotIn("db_avg", response)
        self.assertEqual(self.table.state["db_last_sample_time"], last_valid)
        self.epoch += 2
        self.assertEqual(self.poll(80)["db_avg"], 80)

    def test_app_poll_does_not_claim_an_expired_sample_is_valid_or_update_measurements(self):
        self.poll(80)
        before = copy.deepcopy(self.table.state)
        self.epoch += 10
        response = self.device.handle_device("POLL", "ambient", {"email": "user@example.com"}, "SS", [100] * 4)
        body = json.loads(response["body"])
        self.assertFalse(body["microphone_valid"])
        self.assertNotIn("db_level", body)
        self.assertFalse(body["db_window_ready"])
        self.assertEqual(self.table.state, before)

    def test_gap_during_probe_requires_a_new_three_second_span(self):
        self.start()
        self.epoch += 2
        self.poll()
        self.epoch += 2
        self.poll(90)
        self.epoch += 11
        self.assertEqual(self.poll(40)["spray"], -1)
        self.epoch += 2
        self.assertEqual(self.poll(40)["spray"], -1)
        self.epoch += 2
        self.assertEqual(self.poll(40)["duration"], 60)
        self.assertEqual(self.table.state["ambient_startup_avg"], 40)

    def test_stop_during_probe_cannot_start_spraying_later(self):
        self.start()
        self.epoch += 2
        self.poll()
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
        for _ in range(5):
            self.epoch += 2
            response = self.poll(90)
            self.assertEqual(response["active_mode"], "ready")
            self.assertLessEqual(response["spray"], 0)
        self.api.ask_gemini_ambient_mood.assert_not_called()

    def test_probe_finishes_at_three_seconds_with_one_second_polls(self):
        self.start()
        self.epoch += 1
        self.poll()  # stop delivery; exclude its sample
        for elapsed in range(4):
            self.epoch += 1
            response = self.poll(90)
            if elapsed < 3:
                self.assertEqual(response["spray"], -1)
        self.assertEqual((response["spray"], response["duration"]), (4, 60))

    def test_old_poll_cannot_contaminate_a_new_probe_session(self):
        self.start()
        original_update = self.table.update_item

        def new_session_before_write(**kwargs):
            if "ambient_startup_samples" in kwargs["UpdateExpression"]:
                self.table.state.update(ambient_startup_token="new-session",
                                        ambient_startup_samples=[], ambient_startup_awaiting_poll=True)
            return original_update(**kwargs)

        self.table.update_item = new_session_before_write
        self.epoch += 2
        self.poll(90)
        self.assertEqual(self.table.state["ambient_startup_samples"], [])
        self.assertTrue(self.table.state["ambient_startup_awaiting_poll"])
        self.api.ask_gemini_ambient_mood.assert_not_called()

    def test_fractional_timestamps_do_not_shorten_three_second_measurement(self):
        self.start()
        self.epoch += 0.1
        self.poll()
        self.epoch += 0.8
        self.assertEqual(self.poll(50)["spray"], -1)
        self.epoch += 2.2
        self.assertEqual(self.poll(50)["spray"], -1)
        self.epoch += 1.0
        self.assertEqual(self.poll(50)["duration"], 60)

    def app_poll(self):
        response = self.device.handle_device("POLL", "weather", {"email": "test@example.com"}, "SS", [100] * 4)
        self.assertEqual(response["statusCode"], 200, response)
        return json.loads(response["body"])

    def test_renewal_is_due_at_thirty_seconds_and_does_not_repeat_each_poll(self):
        self.start_selected()
        initial_expiry = self.table.state["ambient_lease_expires_at"]
        for _ in range(14):
            self.epoch += 2
            response = self.poll()
            self.assertEqual((response["spray"], response["app_command"]), (-1, False))
            self.assertEqual(self.table.state["ambient_lease_expires_at"], initial_expiry)
        self.epoch += 2
        response = self.poll()
        self.assertEqual((response["spray"], response["duration"], response["music"]), (2, 60, 0))
        self.assertEqual(self.table.state["ambient_lease_expires_at"], self.epoch + 60)
        self.epoch += 2
        self.assertEqual(self.poll()["spray"], -1)

    def test_analysis_window_and_actuator_duration_are_independent(self):
        with patch.object(self.config, "AMBIENT_ANALYSIS_WINDOW_SECONDS", 120):
            self.assertEqual(self.start_selected()["duration"], 60)

    def test_app_poll_cannot_renew_or_consume_the_device_lease(self):
        self.start_selected()
        initial_expiry = self.table.state["ambient_lease_expires_at"]
        self.epoch += 30
        self.app_poll()
        self.assertEqual(self.table.state["ambient_lease_expires_at"], initial_expiry)
        self.assertEqual(self.table.state["pending_cmd"], -1)
        self.assertTrue(self.db.renew_ambient_lease("SS", self.epoch))
        self.assertEqual(self.app_poll()["duration"], 60)
        self.assertEqual(self.table.state["pending_cmd"], 2)
        self.assertNotIn("ambient_lease_expires_at", self.table.state)
        self.epoch += 4
        self.assertEqual(self.poll()["duration"], 60)
        self.assertEqual(self.table.state["ambient_lease_expires_at"], self.epoch + 60)

    def test_disconnection_has_no_automatic_extension_from_app_polls(self):
        self.start_selected()
        expiry = self.pump_expires_at
        self.epoch += 61
        self.app_poll()
        self.assertEqual(self.pump_expires_at, expiry)
        self.assertLess(expiry, self.epoch)
        self.assertEqual(self.table.state["ambient_lease_expires_at"], expiry)

    def test_music_changes_only_when_different_and_app_peek_cannot_consume_it(self):
        self.start_selected()
        self.table.state["current_music"] = 7
        self.assertEqual(self.app_poll()["music"], 7)
        self.epoch += 2
        self.assertEqual(self.poll()["music"], 7)
        self.epoch += 2
        self.assertEqual(self.poll()["music"], 0)

    def test_stop_clears_queued_renewal_and_cannot_be_undone_by_future_polls(self):
        self.start_selected()
        self.epoch += 30
        self.assertTrue(self.db.renew_ambient_lease("SS", self.epoch))
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
        self.assertFalse(self.table.state["ambient_renewal_enabled"])
        self.assertNotIn("ambient_renew_at", self.table.state)
        self.assertNotIn("ambient_lease_token", self.table.state)
        self.assertEqual(self.poll()["spray"], 0)
        for _ in range(35):
            self.epoch += 2
            response = self.poll()
            self.assertEqual(response["active_mode"], "ready")
            self.assertEqual(response["spray"], -1)

    def test_stop_wins_while_a_renewal_write_is_pending(self):
        self.start_selected()
        original_update = self.table.update_item

        def stop_before_renewal(**kwargs):
            if ":lease_token" in kwargs.get("ExpressionAttributeValues", {}):
                self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
            return original_update(**kwargs)

        self.table.update_item = stop_before_renewal
        self.epoch += 30
        response = self.poll()
        self.assertEqual((response["active_mode"], response["spray"]), ("ready", 0))
        self.assertFalse(self.table.state["ambient_renewal_enabled"])

    def test_stop_wins_while_an_old_mailbox_command_is_being_delivered(self):
        self.start_selected()
        self.epoch += 30
        self.db.renew_ambient_lease("SS", self.epoch)
        original_update = self.table.update_item

        def stop_before_delivery(**kwargs):
            if ":expires" in kwargs.get("ExpressionAttributeValues", {}):
                self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
            return original_update(**kwargs)

        self.table.update_item = stop_before_delivery
        response = self.poll()
        self.assertEqual(response["active_mode"], "ready")
        self.assertLessEqual(response["spray"], 0)
        self.assertEqual(self.table.state["pending_cmd"], 0)

    def test_stop_during_bucket_music_selection_cannot_reactivate_ambient(self):
        self.start_selected()
        select_music = self.db.select_ambient_music

        def stop_before_save(*args, **kwargs):
            music = select_music(*args, **kwargs)
            self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
            return music

        with patch.object(self.db, "select_ambient_music", side_effect=stop_before_save):
            response = self.ambient.handle_ambient("ambient", {"effective_db": 90, "force_spray": True}, "SS", [100] * 4)
        self.assertEqual(json.loads(response["body"])["spray"], -1)
        self.assertEqual(self.table.state["active_mode"], "ready")
        self.assertEqual(self.table.state["pending_cmd"], 0)

    def test_dislike_replacement_uses_sixty_seconds_and_gets_renewed(self):
        self.start_selected()
        response = self.feedback.handle_feedback("DISLIKE", "ambient", {
            "value": -5, "current_scent_id": 2, "ambient_context": self.table.state["ambient_context"],
        }, "SS")
        self.assertEqual(response["statusCode"], 200, response)
        self.epoch += 2
        self.assertEqual((self.poll()["spray"], self.spray_commands[-1][2]), (4, 60))
        self.epoch += 30
        self.assertEqual(self.poll()["spray"], 4)

    def test_display_stop_clears_the_renewal_schedule(self):
        self.start_selected()
        self.epoch += 30
        self.db.renew_ambient_lease("SS", self.epoch)
        response = self.device.handle_device("DISPLAY_MODE", "manual", {
            "active_mode": "ready", "active_scent": 0,
        }, "SS", [100] * 4)
        self.assertEqual(response["statusCode"], 200, response)
        self.assertFalse(self.table.state["ambient_renewal_enabled"])
        self.assertEqual(self.table.state["pending_cmd"], 0)
        self.assertNotIn("ambient_renew_at", self.table.state)

    def test_another_output_command_disables_the_old_ambient_renewal(self):
        self.start_selected()
        self.db.manage_mailbox("SS", 1, "Manual", 3)
        self.assertFalse(self.table.state["ambient_renewal_enabled"])
        self.assertNotIn("ambient_renew_at", self.table.state)
        self.epoch += 31
        self.assertFalse(self.db.renew_ambient_lease("SS", self.epoch))
        self.assertEqual(self.table.state["pending_cmd"], 1)

    def test_stop_while_selecting_a_dislike_replacement_wins(self):
        self.start_selected()

        def stop_before_choice(*args):
            self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)
            return 4

        self.db.get_ambient_alternative.side_effect = stop_before_choice
        response = self.feedback.handle_feedback("DISLIKE", "ambient", {
            "value": -5, "current_scent_id": 2, "ambient_context": self.table.state["ambient_context"],
        }, "SS")
        self.assertEqual(json.loads(response["body"])["spray"], -1)
        self.assertEqual(self.table.state["active_mode"], "ready")
        self.assertEqual(self.table.state["pending_cmd"], 0)
        self.db.schedule_implicit_like.assert_not_called()

    def test_off_display_mode_also_clears_scent_and_renewal(self):
        self.start_selected()
        self.device.handle_device("DISPLAY_MODE", "manual", {
            "active_mode": "off", "active_scent": 2,
        }, "SS", [100] * 4)
        self.assertEqual(self.table.state["active_scent"], 0)
        self.assertFalse(self.table.state["ambient_renewal_enabled"])

    def test_authorization_longer_than_firmware_limit_is_not_saved(self):
        self.start_selected()
        before = copy.deepcopy(self.table.state)
        saved = self.db.manage_mailbox(
            "SS", 2, "Night_Quiet", 271, save_active_mode="ambient",
            save_ambient_lease_duration=271, expected_initial_state=before,
        )
        self.assertIsNone(saved)
        self.assertEqual(self.table.state, before)

    def test_bucket_change_does_not_select_a_cartridge_below_firmware_cutoff(self):
        self.start_selected()
        self.sensor_weights = [100, 100, 100, 10]
        self.api.ask_gemini_ambient_mood.return_value = (7, {
            "led_r": 0, "led_g": 0, "led_b": 255, "led_bright": 150,
        })
        base_epoch = self.epoch
        for elapsed in range(2, 63, 2):
            self.epoch = base_epoch + elapsed
            response = self.poll(90)
        # Loud prefers slot 4, but the real firmware rejects it below 15%.
        # Slot 1 is the next configured usable scent, so music and the pump
        # command must switch together to an actually usable cartridge.
        # Track 7 is Quiet-only; the invalid AI choice falls back to Loud track 1.
        self.assertEqual((response["music"], response["spray"], response["duration"]), (1, 1, 60))

    def test_scent_kind_is_mapped_to_the_actual_physical_slot(self):
        self.db.get_spray_mapping = lambda device_id: {1: 4, 2: 2, 3: 1, 4: 5}
        response = self.start_selected()
        self.assertEqual(response["spray"], 4)  # Quiet chooses lavender, kind 5 in slot 4
        self.sensor_weights = [100] * 4
        base_epoch = self.epoch
        for elapsed in range(2, 63, 2):
            self.epoch = base_epoch + elapsed
            response = self.poll(90)
        self.assertEqual(response["spray"], 1)  # Loud chooses peppermint, kind 4 in slot 1

    def test_duplicate_scent_uses_the_full_slot_instead_of_an_empty_duplicate(self):
        self.db.get_spray_mapping = lambda device_id: {1: 4, 2: 2, 3: 4, 4: 1}
        self.start_selected()
        self.sensor_weights = [100, 100, 0, 100]
        base_epoch = self.epoch
        for elapsed in range(2, 63, 2):
            self.epoch = base_epoch + elapsed
            response = self.poll(90)
        self.assertEqual(response["spray"], 1)

    def test_ambient_selection_respects_fifteen_percent_boundary_and_invalid_levels(self):
        for percent, expected_slot in [(14.9, 1), (15, 4), (float("nan"), 1), (None, 1)]:
            with self.subTest(percent=percent):
                _, slot, _ = self.db.get_ambient_spray_recommendation("Loud", "SS", [100, 100, 100, percent])
                self.assertEqual(slot, expected_slot)

    def test_no_usable_new_cartridge_does_not_change_music_alone(self):
        self.start_selected()
        self.sensor_weights = [0, 0, 0, 0]
        base_epoch = self.epoch
        for elapsed in range(2, 63, 2):
            self.epoch = base_epoch + elapsed
            response = self.poll(90)
        self.api.ask_gemini_ambient_mood.assert_called_once()
        self.assertEqual(response["music"], 0)
        self.assertEqual(self.table.state["current_music"], 7)  # Existing Quiet playlist continues.

    def test_all_bucket_transitions_change_music_without_waiting_for_ai(self):
        self.start_selected(level=50)
        # Failure in the optional external service must not block a new bucket.
        self.api.ask_gemini_ambient_mood.side_effect = RuntimeError("AI unavailable")
        for level, bucket in ((70, "Moderate"), (90, "Loud"), (50, "Quiet"),
                              (90, "Loud"), (70, "Moderate"), (50, "Quiet")):
            with self.subTest(bucket=bucket, level=level):
                start = self.epoch
                for elapsed in range(2, 65, 2):
                    self.epoch = start + elapsed
                    response = self.poll(level)
                self.assertTrue(response["db_window_ready"])
                self.assertTrue(response["ambient_context"].endswith("_" + bucket))
                self.assertIn(self.playing_music, self.config.AMBIENT_MUSIC_BY_BUCKET[bucket])
                self.assertEqual(response["ambient_bucket_music_policy"], "immediate-bucket-v1")
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_bucket_music_changes_even_if_the_only_usable_scent_stays_the_same(self):
        self.db.get_spray_mapping = lambda device_id: {1: 6, 2: 0, 3: 0, 4: 0}
        self.assertEqual(self.start_selected(level=50)["spray"], 1)
        self.sensor_weights = [100, 0, 0, 0]
        self.api.ask_gemini_ambient_mood.side_effect = RuntimeError("AI unavailable")
        start = self.epoch
        commands = []
        for elapsed in range(2, 65, 2):
            self.epoch = start + elapsed
            response = self.poll(70)
            if response["music"] > 0:
                commands.append(response)
        self.assertEqual(response["active_scent"], 1)
        self.assertTrue(response["ambient_context"].endswith("_Moderate"))
        self.assertIn(self.playing_music, self.config.AMBIENT_MUSIC_BY_BUCKET["Moderate"])
        self.assertTrue(any(cmd["spray"] == 1 and cmd["music"] in self.config.AMBIENT_MUSIC_BY_BUCKET["Moderate"]
                            for cmd in commands))
        self.api.ask_gemini_ambient_mood.assert_called_once()

    def test_failed_bucket_command_does_not_publish_a_different_effective_bucket(self):
        self.start_selected(level=50)
        applied_context = self.table.state["ambient_context"]
        applied_level = self.table.state["ambient_effective_db"]
        original_mailbox = self.db.manage_mailbox

        def reject_new_bucket(*args, **kwargs):
            context = kwargs.get("save_ambient_metadata", {}).get("ambient_context", "")
            if context and context != applied_context:
                return None
            return original_mailbox(*args, **kwargs)

        start = self.epoch
        with patch.object(self.db, "manage_mailbox", side_effect=reject_new_bucket):
            for elapsed in range(2, 65, 2):
                self.epoch = start + elapsed
                response = self.poll(90)
        self.assertGreaterEqual(response["db_avg"], 80)
        self.assertEqual(response["ambient_context"], applied_context)
        self.assertEqual(response["ambient_effective_db"], float(applied_level))


if __name__ == "__main__":
    unittest.main()
