"""Local diary feedback tests with real handlers and an offline state table."""

import ast
import copy
import importlib
import importlib.util
import json
import logging
import math
from pathlib import Path
import random
import sys
import types
import unittest
import uuid
from datetime import datetime, timedelta, timezone
from unittest.mock import Mock, patch

import test_ambient_initial as ambient_support


LAMBDA_DIR = Path(__file__).resolve().parents[1] / "Lambda"


class DiaryFeedbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config_spec = importlib.util.spec_from_file_location("diary_test_config", LAMBDA_DIR / "config.py")
        cls.config = importlib.util.module_from_spec(config_spec)
        config_spec.loader.exec_module(cls.config)
        cls.db = types.ModuleType("db_utils")
        package = types.ModuleType("diary_test_handlers")
        package.__path__ = [str(LAMBDA_DIR / "handlers")]
        cls.modules = patch.dict(sys.modules, {
            "config": cls.config, "db_utils": cls.db, "api_utils": types.ModuleType("api_utils"),
            "diary_test_handlers": package,
        })
        cls.modules.start()
        cls.feedback = importlib.import_module("diary_test_handlers.feedback_handler")
        cls.device = importlib.import_module("diary_test_handlers.device_handler")
        cls.core = importlib.import_module("diary_test_handlers.core_mode_handler")
        source = ast.parse((LAMBDA_DIR / "db_utils.py").read_text(encoding="utf-8-sig"))
        functions = [node for node in source.body if isinstance(node, ast.FunctionDef)
                     and node.name in ("manage_mailbox", "get_preferred_alternative", "get_music_track", "activate_diary_playback")]
        cls.db.__dict__.update(config=cls.config, uuid=uuid, random=random, math=math,
                               logger=logging.getLogger("diary-feedback-tests"))
        exec(compile(ast.Module(body=functions, type_ignores=[]), "db_utils.py", "exec"), cls.db.__dict__)

    @classmethod
    def tearDownClass(cls):
        cls.modules.stop()

    def setUp(self):
        self.epoch = 1800000000
        self.mapping = {1: 1, 2: 2, 3: 3, 4: 4}
        self.table = ambient_support.StateTable()
        self.table.state = {
            "active_mode": "manual", "active_scent": 4, "current_spray": 4,
            "current_music": 16, "pending_cmd": -1, "weights": [100] * 4,
            "music_tracks": "1_6_11_16",
        }
        self.db.state_table = self.table
        self.db.get_device_state = lambda device: copy.deepcopy(self.table.state)
        self.db.get_spray_mapping = lambda device: self.mapping
        self.db.get_kind_from_spray = lambda device, slot: self.mapping.get(slot)
        self.db.get_spray_from_kind = lambda device, kind: next(
            (slot for slot, loaded in self.mapping.items() if loaded == kind), None)
        self.db._kind_ids = lambda: list(self.config.KIND_IDS)
        self.db._active_kinds = lambda device: list(self.mapping.values())
        self.db._get_email_by_device_id = lambda device: "offline@example.invalid"
        self.db._get_kst_now = lambda: datetime.fromtimestamp(self.epoch, timezone(timedelta(hours=9)))
        self.db.get_saved_region = lambda device: ""
        self.db.apply_due_implicit_like = Mock(return_value=None)
        self.db.cancel_implicit_like = Mock()
        self.db.schedule_implicit_like = Mock(return_value=True)
        self.db.save_feedback = Mock(return_value=True)
        # A disabled cartridge has the highest preference: it must be filtered.
        self.db.pref_table = types.SimpleNamespace(get_item=Mock(return_value={"Item": {
            "scent_1_score": 1, "scent_2_score": 100, "scent_3_score": 10,
        }}))
        clock = patch.object(self.device.time, "time", side_effect=lambda: self.epoch)
        clock.start()
        self.addCleanup(clock.stop)

    def stop(self):
        self.device.handle_device("STOP_ALL", "menu", {}, "SS", [100] * 4)

    def dislike(self):
        response = self.feedback.handle_feedback("FEEDBACK", "manual", {
            "feedback_action": "DISLIKE", "value": -5, "current_scent_id": -1,
            "data": "4_AI_DIARY", "region": "4_AI_DIARY", "email": "offline@example.invalid",
        }, "SS")
        self.assertEqual(response["statusCode"], 200)
        return json.loads(response["body"])

    def poll(self, app=False):
        payload = {"email": "offline@example.invalid"} if app else {"music": 0}
        response = self.device.handle_device("POLL", "manual", payload, "SS", [100] * 4)
        self.assertEqual(response["statusCode"], 200)
        return json.loads(response["body"])

    def test_stop_dislike_then_hardware_poll_delivers_replacement_scent_and_music(self):
        self.stop()
        self.epoch += 3
        feedback = self.dislike()
        self.assertEqual(feedback["result"], "SUCCESS")
        self.assertEqual((feedback["active_mode"], feedback["spray"], feedback["music"], feedback["duration"]),
                         ("manual", 3, 11, 3))
        poll = self.poll()
        self.assertEqual((poll["active_mode"], poll["active_scent"], poll["spray"], poll["app_command"]),
                         ("manual", 3, 3, True))
        self.assertEqual((poll["music"], poll["duration"]), (11, 3))
        self.assertEqual(self.table.state["pending_cmd"], -1)

    def test_app_refresh_cannot_consume_the_replacement_before_hardware_poll(self):
        self.stop()
        self.dislike()
        self.assertEqual(self.poll(app=True)["spray"], 3)
        self.assertEqual(self.table.state["pending_cmd"], 3)
        self.assertEqual(self.poll()["spray"], 3)

    def test_disabled_second_cartridge_is_excluded_despite_highest_preference(self):
        self.stop()
        response = self.dislike()
        self.assertEqual(response["spray"], 3)
        self.db.save_feedback.assert_called_once_with("SS", "AI_DIARY", 4, -5, "offline@example.invalid")

    def test_empty_replacement_cartridge_is_excluded(self):
        self.table.state["weights"][2] = 0
        self.stop()
        response = self.dislike()
        self.assertEqual((response["spray"], response["music"]), (1, 1))

    def test_a_different_configured_song_is_preferred_to_repeating_the_rejected_song(self):
        self.table.state["music_tracks"] = "1_6_16_16"
        self.stop()
        response = self.dislike()
        self.assertEqual((response["spray"], response["music"]), (1, 1))

    def test_no_usable_alternative_returns_failure_without_claiming_a_new_spray(self):
        self.table.state["weights"] = [0, 100, 0, 100]
        self.stop()
        response = self.dislike()
        self.assertEqual(response["result"], "FAIL")
        self.assertNotIn("spray", response)
        self.assertEqual((self.table.state["active_mode"], self.table.state["pending_cmd"]), ("ready", 0))
        self.db.schedule_implicit_like.assert_not_called()

    def test_mailbox_write_failure_does_not_return_success_or_schedule_a_like(self):
        self.stop()
        with patch.object(self.db, "manage_mailbox", return_value=None):
            response = self.dislike()
        self.assertEqual(response["result"], "FAIL")
        self.assertNotIn("spray", response)
        self.db.schedule_implicit_like.assert_not_called()

    def test_a_new_stop_during_selection_prevents_the_old_feedback_from_restarting(self):
        self.stop()
        select = self.db.get_preferred_alternative

        def stop_during_selection(*args, **kwargs):
            choice = select(*args, **kwargs)
            self.epoch += 2
            self.stop()
            return choice

        with patch.object(self.db, "get_preferred_alternative", side_effect=stop_during_selection):
            response = self.dislike()
        self.assertEqual(response["result"], "FAIL")
        self.assertEqual((self.table.state["active_mode"], self.table.state["pending_cmd"]), ("ready", 0))

    def test_explicit_like_only_saves_preference_without_replacing_the_scent(self):
        before = copy.deepcopy(self.table.state)
        response = self.feedback.handle_feedback("LIKE", "manual", {
            "current_scent_id": 4, "context": "AI_DIARY", "value": 1, "email": "offline@example.invalid",
        }, "SS")
        self.assertEqual(response["statusCode"], 200)
        self.assertEqual(self.table.state, before)
        self.db.save_feedback.assert_called_once_with("SS", "AI_DIARY", 4, 1, "offline@example.invalid")

    def test_lavender_vanilla_blend_uses_a_canonical_command_and_a_participating_slot_song(self):
        self.mapping = {1: 6, 2: 2, 3: 4, 4: 5}
        self.table.state["music_tracks"] = "1,2,3,4_5,6,7,8_9,10,11,12_13,14,15,16"
        for kinds in (56, 65):
            with self.subTest(kinds=kinds):
                command = self.core._map_diary_kind_to_safe_pumps(kinds, self.mapping, {2})
                self.assertEqual(command, 14)  # 41 skips the device's <=34 music branch.
                track = self.db.get_music_track("SS", command, is_pump=True)
                self.assertEqual(track, 1)  # First configured song of slot 1, in command order.
                self.assertIn(track, {1, 13})
        self.assertEqual(self.db.get_music_track("SS", 4, is_pump=True), 13)

    def test_all_blend_pairs_are_identical_in_both_ai_kind_orders(self):
        for first in range(1, 5):
            for second in range(first + 1, 5):
                for kinds in (first * 10 + second, second * 10 + first):
                    with self.subTest(kinds=kinds):
                        command = self.core._map_diary_kind_to_safe_pumps(kinds, self.mapping, set())
                        self.assertEqual(command, first * 10 + second)
                        self.assertIn(command, (12, 13, 14, 23, 24, 34))

    def test_blend_skips_a_silent_first_slot_and_uses_the_other_participating_slot(self):
        self.table.state["music_tracks"] = "0_6_11_13,14,15,16"
        self.assertEqual(self.db.get_music_track("SS", 14, is_pump=True), 13)

    def test_single_cartridge_keeps_its_configured_first_song(self):
        self.table.state["music_tracks"] = "1,2,3,4_5,6,7,8_9,10,11,12_13,14,15,16"
        for slot, expected in ((1, 1), (2, 5), (3, 9), (4, 13)):
            self.assertEqual(self.db.get_music_track("SS", slot, is_pump=True), expected)

    def queue_diary_blend(self):
        self.mapping = {1: 6, 2: 2, 3: 4, 4: 5}
        self.table.state["music_tracks"] = "1,2,3,4_5,6,7,8_9,10,11,12_13,14,15,16"
        self.db.get_user_history_text = Mock(return_value="")
        self.db.get_personalized_scent = lambda device, context, kind, email: (kind, "")
        self.db.save_diary = Mock(return_value=True)
        self.db.update_device_state = Mock()
        self.db.log_table = types.SimpleNamespace(put_item=Mock())
        self.core.api_utils.ask_gemini_recommendation = Mock(return_value=(
            "56", "라벤더와 바닐라를 추천합니다.",
            {"led_r": 100, "led_g": 0, "led_b": 255, "led_bright": 180}, "불안한 하루", "불안함",
        ))
        response = self.core.handle_core_modes("AI_EMOTION", "emotion", {
            "diary_text": "졸업을 앞두고 불안한 하루였습니다.", "email": "offline@example.invalid",
        }, "SS", [100] * 4, "분석중")
        self.assertEqual(response["statusCode"], 200)
        body = json.loads(response["body"])
        self.assertEqual((body["spray"], body["music"]), (14, 1))
        self.assertEqual((body["led_r"], body["led_g"], body["led_b"]), (100, 0, 255))
        return body

    def test_diary_ai_blend_response_and_hardware_poll_choose_the_same_cartridge_song(self):
        self.queue_diary_blend()
        self.assertEqual(self.poll()["spray"], 0)  # Clear any previous device playlist first.
        poll = self.poll()
        self.assertEqual((poll["spray"], poll["music"], poll["app_command"]), (14, 1, True))
        self.assertEqual(poll["music_tracks"], "1_0_0_0")

    def test_fixed_first_song_does_not_replace_the_users_saved_playlists(self):
        self.queue_diary_blend()
        saved_tracks = self.table.state["music_tracks"]
        self.poll()
        started = self.poll()
        self.assertEqual(started["music_tracks"], "1_0_0_0")
        self.assertEqual(self.table.state["music_tracks"], saved_tracks)
        self.assertEqual(self.poll(app=True)["music_tracks"], saved_tracks)

    def test_fixed_diary_playlist_contains_only_the_chosen_first_song_on_later_polls(self):
        self.queue_diary_blend()
        self.poll()
        self.poll()
        for _ in range(4):
            response = self.poll()
            playlist = [int(track) for slot in response["music_tracks"].split("_")
                        for track in slot.split(",") if int(track) > 0]
            self.assertEqual(playlist, [1])
            self.assertEqual(response["spray"], -1)

    def test_a_single_cartridge_replacement_has_only_its_first_song_in_hardware_playlist(self):
        self.stop()
        self.dislike()
        response = self.poll()
        self.assertEqual((response["spray"], response["music"], response["music_tracks"]), (3, 11, "0_0_11_0"))

    def test_new_diary_waits_until_the_previous_track_is_reported_stopped(self):
        self.queue_diary_blend()
        self.assertEqual(self.poll()["spray"], 0)
        response = self.device.handle_device("POLL", "manual", {"music": 15}, "SS", [100] * 4)
        self.assertEqual(json.loads(response["body"])["active_mode"], "ready")
        self.assertTrue(self.table.state["diary_restart_pending"])
        self.assertEqual(self.poll()["spray"], 14)

    def test_app_peeks_cannot_advance_the_diary_restart(self):
        self.queue_diary_blend()
        self.assertEqual(self.poll(app=True)["spray"], 0)
        self.assertEqual(self.table.state["pending_cmd"], 0)
        self.assertEqual(self.poll()["spray"], 0)
        self.assertEqual(self.poll(app=True)["active_mode"], "ready")
        self.assertTrue(self.table.state["diary_restart_pending"])

    def test_user_stop_cancels_a_queued_diary_restart(self):
        self.queue_diary_blend()
        self.poll()
        self.stop()
        self.assertNotIn("diary_restart_pending", self.table.state)
        self.assertEqual(self.poll()["spray"], 0)
        self.assertEqual(self.poll()["active_mode"], "ready")

    def test_later_manual_command_restores_the_original_playlist_delivery(self):
        self.queue_diary_blend()
        self.poll()
        self.poll()
        self.db.manage_mailbox("SS", 3, save_active_mode="manual", save_active_scent=3)
        response = self.poll()
        self.assertNotIn("diary_music_track", self.table.state)
        self.assertEqual(response["music_tracks"], self.table.state["music_tracks"])

    def test_stop_wins_over_a_diary_restart_write_in_flight(self):
        self.queue_diary_blend()
        self.poll()
        update = self.table.update_item

        def stop_before_start(**kwargs):
            if ":diary_music" in kwargs.get("ExpressionAttributeValues", {}) and ":diary_pending" not in kwargs["ExpressionAttributeValues"]:
                self.stop()
            return update(**kwargs)

        self.table.update_item = stop_before_start
        response = self.poll()
        self.assertEqual(response["active_mode"], "ready")
        self.assertLessEqual(response["spray"], 0)


if __name__ == "__main__":
    unittest.main()
