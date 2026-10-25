# 주변 소음(dB)을 바탕으로 무드와 향기를 자동 추천하는 앰비언트 모드 핸들러임


#기본 대기 모드 및 기본 조명 제어를 담당하는 람다 핸들러

import json
import logging
from datetime import datetime, timedelta, timezone
from decimal import Decimal

import config
import db_utils
import api_utils

from .common import (
    _get_kst_now, _parse_int, _build_ambient_context
)

logger = logging.getLogger()

# 주변 소음에 맞춰 조명과 향기를 추천함
def handle_ambient(mode, body, device_id, normalized_weights):
    if mode == "ambient":
        try:
            now_kst = _get_kst_now()
            
            state = db_utils.get_device_state(device_id) or {}
            db_avg = float(body.get("db_avg", state.get("db_avg", state.get("db_level", 40))))
            db_1hr_avg = float(body.get("db_1hr_avg", state.get("db_1hr_avg", db_avg)))
            is_spike = bool(body.get("is_spike", state.get("is_spike", False)))
            db_stddev = float(body.get("db_stddev", state.get("db_stddev", 0.0)))
            effective_db = float(
                body.get("effective_db", state.get("ambient_effective_db", 40))
            )

            context_key, time_slot, noise_bucket, _ = _build_ambient_context(effective_db, now_kst)
            db_utils.seed_ambient_defaults()

            last_time_str = state.get('last_spray_time', "2000-01-01T00:00:00")
            current_capacity = float(state.get('current_capacity', config.MAX_CAPACITY))
            last_spray_code = int(state.get('last_spray_code', 0))
            current_spray = int(
                state.get('active_scent', 0)
                or state.get('current_spray', 0)
                or last_spray_code
            )

            # Weights preservation
            state_weights = body.get('weights', state.get('weights', normalized_weights))
            preserved_weights = []
            for weight in state_weights if isinstance(state_weights, list) else normalized_weights:
                try: preserved_weights.append(float(weight))
                except Exception: preserved_weights.append(0.0)
            while len(preserved_weights) < 4: preserved_weights.append(0.0)
            preserved_weights = preserved_weights[:4]

            # A sustained-noise reevaluation changes only the background
            # music. Scent, pump state and LED settings stay untouched.
            if bool(body.get("music_only", False)):
                current_music = int(state.get("current_music", 0) or 0)
                music_code, _ = api_utils.ask_gemini_ambient_mood(
                    effective_db,
                    db_1hr_avg,
                    False,
                    db_stddev,
                )
                music_code = int(music_code or 0)
                if music_code <= 0 or music_code == current_music:
                    music_code = (current_music % 25) + 1

                db_utils.state_table.update_item(
                    Key={"deviceId": device_id},
                    UpdateExpression=(
                        "set active_mode = :mode, current_music = :music, "
                        "ambient_last_eval_time = :t, ambient_context = :ctx, "
                        "db_avg = :avg, db_stddev = :std, is_spike = :sp"
                    ),
                    ExpressionAttributeValues={
                        ":mode": "ambient",
                        ":music": music_code,
                        ":t": int(now_kst.timestamp()),
                        ":ctx": context_key,
                        ":avg": Decimal(str(round(db_avg, 2))),
                        ":std": Decimal(str(round(db_stddev, 2))),
                        ":sp": False,
                    },
                )
                logger.info(
                    f"[AMBIENT_SUSTAINED_MUSIC_CHANGE] ID:{device_id} "
                    f"DB:{db_avg} Music:{current_music}->{music_code}"
                )
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "status": "SUCCESS",
                        "message": "Music changed after sustained noise",
                        "spray": -1,
                        "app_command": False,
                        "duration": 0,
                        "music": music_code,
                        "active_mode": "ambient",
                        "active_scent": int(current_spray),
                        "ambient_context": context_key,
                        "db_level": int(state.get("db_level", db_avg)),
                        "db_avg": round(db_avg, 2),
                        "db_stddev": round(db_stddev, 2),
                        "is_spike": False,
                    }, ensure_ascii=False),
                }

            # Roulette Scent Recommendation (AI 제외, 소음 알고리즘 단독 사용)
            best_kind, ambient_scores = db_utils.get_ambient_recommendation(context_key, device_id)
            spray_code = db_utils.get_spray_from_kind(device_id, best_kind)
            if not spray_code:
                spray_code = 1

            is_entering_ambient = state.get("active_mode") != "ambient"
            spray_changed = int(spray_code) != int(current_spray)
            should_spray = bool(
                spray_code > 0
                and (is_entering_ambient or current_spray <= 0 or spray_changed)
            )

            # 10-second Cooldown & Fast-track logic (테스트용)
            COOLDOWN_SECONDS = 10 # 향 교체 직후의 최소 보호 시간
            is_blocked = False
            
            if should_spray and spray_changed:
                try:
                    last_time = datetime.fromisoformat(last_time_str)
                    if (now_kst - last_time).total_seconds() < COOLDOWN_SECONDS:
                        is_blocked = True
                except Exception:
                    pass

            if is_blocked:
                logger.info(f"[{device_id}] Ambient cooldown active (Scent {last_spray_code}->{spray_code}). (is_spike={is_spike})")
                db_utils.state_table.update_item(
                    Key={"deviceId": device_id},
                    UpdateExpression=(
                        "set active_mode = :mode, active_scent = :scent, "
                        "ambient_last_eval_time = :t, ambient_context = :ctx, "
                        "db_avg = :avg, db_stddev = :std, noise_type = :nt, is_spike = :sp"
                    ),
                    ExpressionAttributeValues={
                        ":mode": "ambient",
                        ":scent": int(current_spray),
                        ":t": int(now_kst.timestamp()),
                        ":ctx": context_key,
                        ":avg": Decimal(str(round(db_avg, 2))),
                        ":std": Decimal(str(round(db_stddev, 2))),
                        ":nt": state.get("noise_type", "분석 중"),
                        ":sp": bool(is_spike),
                    },
                )
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "status": "SUCCESS",
                        "message": "Cooldown active",
                        "spray": 0,
                        "duration": 0,
                        "active_mode": "ambient",
                        "active_scent": int(current_spray),
                        "context": f"Ambient_{context_key}",
                        "ambient_context": context_key,
                        "db_level": int(state.get("db_level", db_avg)),
                        "db_avg": round(db_avg, 2),
                        "db_stddev": round(db_stddev, 2),
                        "noise_type": state.get("noise_type", "분석 중"),
                        "is_spike": is_spike,
                    }, ensure_ascii=False)
                }

            # 순간 스파이크는 추천을 바꾸지 않고, 서버가 확정한 3분 지속 소음만 사용함
            music_code, led_dict = api_utils.ask_gemini_ambient_mood(
                effective_db,
                db_1hr_avg,
                False,
                db_stddev,
            )

            duration = _parse_int(body.get("duration", config.AMBIENT_DURATION_SECONDS), config.AMBIENT_DURATION_SECONDS)

            new_capacity = current_capacity
            if should_spray:
                usage = duration * config.CONSUMPTION_PER_SEC
                new_capacity = round(max(0.0, current_capacity - usage), 2)
                db_utils.update_device_state(device_id, now_kst.isoformat(), new_capacity, spray_code, preserved_weights)
                db_utils.manage_mailbox(
                    device_id,
                    spray_code,
                    context_key,
                    duration,
                    save_active_mode="ambient",
                    save_active_scent=spray_code,
                    save_led=led_dict,
                    save_music=music_code,
                )
            else:
                db_utils.state_table.update_item(
                    Key={"deviceId": device_id},
                    UpdateExpression=(
                        "set active_mode = :mode, active_scent = :scent, "
                        "led_r = :lr, led_g = :lg, led_b = :lb, led_br = :lbr, "
                        "current_music = :music"
                    ),
                    ExpressionAttributeValues={
                        ":mode": "ambient",
                        ":scent": int(current_spray),
                        ":lr": int(led_dict.get("led_r", 255)),
                        ":lg": int(led_dict.get("led_g", 180)),
                        ":lb": int(led_dict.get("led_b", 0)),
                        ":lbr": int(led_dict.get("led_bright", 150)),
                        ":music": int(music_code),
                    },
                )

            db_utils.state_table.update_item(
                Key={"deviceId": device_id},
                UpdateExpression=(
                    "set ambient_last_eval_time = :t, ambient_context = :ctx, "
                    "db_avg = :avg, db_stddev = :std, noise_type = :nt, is_spike = :sp"
                ),
                ExpressionAttributeValues={
                    ":t": int(now_kst.timestamp()),
                    ":ctx": context_key,
                    ":avg": Decimal(str(round(db_avg, 2))),
                    ":std": Decimal(str(round(db_stddev, 2))),
                    ":nt": state.get("noise_type", "분석 중"),
                    ":sp": bool(is_spike),
                },
            )

            response_spray = int(spray_code) if should_spray else 0
            response_duration = int(duration) if should_spray else 0
            response_active_scent = int(spray_code) if should_spray else int(current_spray)

            # DB Log
            try:
                def safe_dec(v): return Decimal(str(v)) if v is not None else Decimal("0")
                db_utils.log_table.put_item(Item={
                    "deviceId": device_id,
                    "timestamp": now_kst.isoformat(),
                    "mode": "Ambient_Mode",
                    "result_text": f"Recommended {spray_code} / Command {response_spray} / Spike: {is_spike}",
                    "spray_code": response_spray,
                    "duration": response_duration,
                    "region": context_key,
                    "weight_g": safe_dec(preserved_weights[0]),
                    "weights": [safe_dec(weight) for weight in preserved_weights],
                    "temp": safe_dec(0),
                    "humid": 0,
                    "feedback": 1,
                    "db_level": int(db_avg)
                })
            except Exception as e:
                logger.warning(f"Ambient log save failed: {e}")

            response_payload = {
                "status": "SUCCESS",
                "message": "Ambient command created" if should_spray else "Ambient recommendation unchanged",
                "cmd": response_spray,
                "spray": response_spray,
                "duration": response_duration,
                "music": int(music_code),
                "led_r": int(led_dict.get("led_r", 255)),
                "led_g": int(led_dict.get("led_g", 180)),
                "led_b": int(led_dict.get("led_b", 0)),
                "led_bright": int(led_dict.get("led_bright", 150)),
                "context": f"Ambient_{context_key}",
                "ambient_context": context_key,
                "active_mode": "ambient",
                "active_scent": response_active_scent,
                "db_level": int(state.get("db_level", db_avg)),
                "db_avg": round(db_avg, 2),
                "db_stddev": round(db_stddev, 2),
                "noise_type": state.get("noise_type", "분석 중"),
                "is_spike": is_spike,
            }

            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps(response_payload, ensure_ascii=False)
            }
        except Exception as e:
            logger.error(f"[AMBIENT_MODE_CRASH] {e}")
            return {
                "statusCode": 500,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "status": "ERROR",
                    "spray": 0,
                    "duration": 0,
                    "message": f"Ambient error: {str(e)}"
                }, ensure_ascii=False)
            }
    
    return None
