# 주변 소음의 초기 측정, 향 선택 및 유한 시간 분사 명령을 처리함.

import json
import logging
from decimal import Decimal

import config
import db_utils
import api_utils
from .common import _get_kst_now, _build_ambient_context, _resolve_ambient_effective_db

logger = logging.getLogger()


def _ambient_wait_response(state, message):
    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({
            "result": "SUCCESS", "status": "SUCCESS", "message": message,
            "active_mode": state.get("active_mode", "ready"),
            "active_scent": int(state.get("active_scent", 0)),
            "spray": -1, "cmd": -1, "app_command": False, "duration": 0, "music": 0,
            "ambient_context": state.get("ambient_context", ""),
            "db_window_ready": bool(state.get("db_window_ready", False)),
            "ambient_startup_pending": bool(state.get("ambient_startup_pending", False)),
        }, ensure_ascii=False),
    }


def handle_ambient(mode, body, device_id, normalized_weights):
    if mode != "ambient":
        return None
    try:
        now_kst = _get_kst_now()
        state = db_utils.get_device_state(device_id) or {}
        startup_token = body.get("_ambient_startup_token")
        completing_startup = bool(
            startup_token
            and state.get("active_mode") == "ambient"
            and state.get("ambient_startup_pending", False)
            and startup_token == state.get("ambient_startup_token")
        )
        if startup_token and not completing_startup:
            return _ambient_wait_response(state, "Ambient measurement superseded")

        action = str(body.get("action", "") or "").strip().lower()
        restarting = (
            action in {"noise", "ambient"}
            and not state.get("ambient_startup_pending", False)
            and not state.get("ambient_renewal_enabled", False)
        )
        if state.get("active_mode") != "ambient" or restarting:
            saved = db_utils.manage_mailbox(
                device_id, 0, save_active_mode="ambient", save_active_scent=0,
                save_music=0, save_ambient_probe_start=int(now_kst.timestamp()),
                expected_initial_state=state,
            )
            latest = db_utils.get_device_state(device_id) or {}
            return _ambient_wait_response(
                latest,
                "Collecting fresh startup sound levels" if saved == 0 else "Ambient start superseded or not saved",
            )

        if state.get("ambient_startup_pending", False) and not completing_startup:
            return _ambient_wait_response(state, "Collecting fresh startup sound levels")
        if action in {"noise", "ambient"} and not completing_startup:
            return _ambient_wait_response(state, "Ambient session already active")

        effective_db = _resolve_ambient_effective_db(body, state)
        db_avg = float(body.get("db_avg", state.get("db_avg", effective_db)))
        db_stddev = float(body.get("db_stddev", state.get("db_stddev", 0)))
        context_key, _, noise_bucket, _ = _build_ambient_context(effective_db, now_kst)
        current_spray = int(state.get("active_scent", 0) or state.get("current_spray", 0) or 0)
        weights = body.get("weights", state.get("weights", normalized_weights))
        if not isinstance(weights, list):
            weights = normalized_weights
        best_kind, spray_code, _ = db_utils.get_ambient_spray_recommendation(
            noise_bucket, device_id, weights,
        )
        if spray_code <= 0:
            return _ambient_wait_response(state, "No usable ambient cartridge is available")
        if not (completing_startup or body.get("force_spray", False) or spray_code != current_spray):
            return _ambient_wait_response(state, "Ambient recommendation unchanged")

        music_code, raw_led = api_utils.ask_gemini_ambient_mood(
            effective_db, float(body.get("db_1hr_avg", db_avg)), False, db_stddev,
        )
        current_music = int(state.get("current_music", 0) or 0)
        music_code = int(music_code or 0)
        if not 1 <= music_code <= 25:
            music_code = current_music
        led_dict = {}
        for field, state_field, default in (
            ("led_r", "led_r", 255), ("led_g", "led_g", 180),
            ("led_b", "led_b", 0), ("led_bright", "led_br", 150),
        ):
            candidate = int((raw_led or {}).get(field, -1))
            led_dict[field] = candidate if 0 <= candidate <= 255 else int(state.get(state_field, default))

        # Analysis time and actuator authorization are separate settings.
        # Every actual scent command permits a finite run, including changes.
        duration = int(config.AMBIENT_DURATION_SECONDS)
        issued_at = _get_kst_now()
        metadata = {
            "ambient_context": context_key,
            "ambient_effective_db": Decimal(str(round(effective_db, 2))),
            "ambient_last_eval_time": int(issued_at.timestamp()),
            "ambient_music_changed_at": int(issued_at.timestamp()),
            "db_avg": Decimal(str(round(db_avg, 2))),
            "db_stddev": Decimal(str(round(db_stddev, 2))),
            "noise_type": str(state.get("noise_type", "분석 중")),
            "is_spike": bool(body.get("is_spike", state.get("is_spike", False))),
        }
        saved = db_utils.manage_mailbox(
            device_id, spray_code, context_key, duration,
            save_active_mode="ambient", save_active_scent=spray_code,
            save_led=led_dict, save_music=music_code,
            save_ambient_window_start=int(issued_at.timestamp()) if completing_startup else None,
            save_ambient_lease_duration=duration, save_ambient_metadata=metadata,
            expected_initial_state=state,
        )
        if saved != spray_code:
            latest = db_utils.get_device_state(device_id) or {}
            return _ambient_wait_response(latest, "Ambient command superseded or not saved")

        logger.info(
            "[AMBIENT_COMMAND] ID:%s Context:%s Kind:%s Slot:%s->%s Duration:%s Weights:%s",
            device_id, context_key, best_kind, current_spray, spray_code, duration, weights,
        )
        capacity = float(state.get("current_capacity", config.MAX_CAPACITY))
        new_capacity = max(0.0, round(capacity - duration * config.CONSUMPTION_PER_SEC, 2))
        db_utils.update_device_state(device_id, issued_at.isoformat(), new_capacity, spray_code, weights)
        try:
            db_utils.log_table.put_item(Item={
                "deviceId": device_id, "timestamp": issued_at.isoformat(),
                "mode": "Ambient_Mode", "spray_code": spray_code, "duration": duration,
                "region": context_key, "db_level": Decimal(str(round(effective_db, 2))),
                "result_text": "Initial ambient run" if completing_startup else "Ambient bucket change",
            })
        except Exception as error:
            logger.warning("Ambient log save failed: %s", error)

        # Hardware outputs are delivered by the mailbox through POLL.
        return {
            "statusCode": 200, "headers": {"Content-Type": "application/json"},
            "body": json.dumps({
                "result": "SUCCESS", "status": "SUCCESS", "message": "Ambient command created",
                "active_mode": "ambient", "active_scent": int(spray_code),
                "spray": int(spray_code), "cmd": int(spray_code),
                "app_command": True, "duration": duration,
                "music": music_code if music_code != current_music else 0,
                **led_dict, "context": "Ambient_" + context_key,
                "ambient_context": context_key, "ambient_effective_db": round(effective_db, 2),
                "ambient_startup_pending": False, "db_level": int(state.get("db_level", db_avg)),
                "db_avg": round(db_avg, 2), "db_stddev": round(db_stddev, 2),
                "noise_type": metadata["noise_type"], "is_spike": metadata["is_spike"],
            }, ensure_ascii=False),
        }
    except Exception as error:
        logger.exception("Ambient command failed")
        return {
            "statusCode": 500, "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"status": "ERROR", "spray": -1, "app_command": False,
                                "duration": 0, "message": str(error)}, ensure_ascii=False),
        }
