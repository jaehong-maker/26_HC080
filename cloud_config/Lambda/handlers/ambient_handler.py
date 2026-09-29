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
            
            db_avg = float(body.get("db_avg", body.get("db_level", 40)))
            db_1hr_avg = float(body.get("db_1hr_avg", db_avg))
            is_spike = bool(body.get("is_spike", False))
            db_stddev = float(body.get("db_stddev", 0.0))
            
            context_key, time_slot, noise_bucket, _ = _build_ambient_context(db_avg, now_kst)
            db_utils.seed_ambient_defaults()

            state = db_utils.get_device_state(device_id) or {}
            last_time_str = state.get('last_spray_time', "2000-01-01T00:00:00")
            current_capacity = float(state.get('current_capacity', config.MAX_CAPACITY))
            last_spray_code = int(state.get('last_spray_code', 0))

            # Weights preservation
            state_weights = body.get('weights', state.get('weights', normalized_weights))
            preserved_weights = []
            for weight in state_weights if isinstance(state_weights, list) else normalized_weights:
                try: preserved_weights.append(float(weight))
                except Exception: preserved_weights.append(0.0)
            while len(preserved_weights) < 4: preserved_weights.append(0.0)
            preserved_weights = preserved_weights[:4]

            # Roulette Scent Recommendation (AI 제외, 소음 알고리즘 단독 사용)
            best_kind, ambient_scores = db_utils.get_ambient_recommendation(context_key, device_id)
            spray_code = db_utils.get_spray_from_kind(device_id, best_kind)
            if not spray_code:
                spray_code = 1 

            # 10-second Cooldown & Fast-track logic (테스트용)
            COOLDOWN_SECONDS = 10 # 10초 쿨타임
            is_blocked = False
            
            if not is_spike:
                try:
                    last_time = datetime.fromisoformat(last_time_str)
                    if (now_kst - last_time).total_seconds() < COOLDOWN_SECONDS:
                        # 바뀐 향이 이전 향과 다를 때만 쿨타임 적용 (같으면 쿨타임 무시하고 통과)
                        if spray_code != last_spray_code:
                            is_blocked = True
                except Exception:
                    pass

            if is_blocked:
                logger.info(f"[{device_id}] Ambient cooldown active (Scent {last_spray_code}->{spray_code}). (is_spike={is_spike})")
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "status": "SUCCESS",
                        "message": "Cooldown active"
                    }, ensure_ascii=False)
                }

            # LLM API Optimization: Only call when not blocked (or spike occurred)
            music_code, led_dict = api_utils.ask_gemini_ambient_mood(db_avg, db_1hr_avg, is_spike, db_stddev)
                
            duration = _parse_int(body.get("duration", config.AMBIENT_DURATION_SECONDS), config.AMBIENT_DURATION_SECONDS)

            new_capacity = current_capacity
            if spray_code > 0:
                usage = duration * config.CONSUMPTION_PER_SEC
                new_capacity = round(max(0.0, current_capacity - usage), 2)
                db_utils.update_device_state(device_id, now_kst.isoformat(), new_capacity, spray_code, preserved_weights)
                db_utils.manage_mailbox(device_id, spray_code, context_key, duration)

            # DB Log
            try:
                def safe_dec(v): return Decimal(str(v)) if v is not None else Decimal("0")
                db_utils.log_table.put_item(Item={
                    "deviceId": device_id,
                    "timestamp": now_kst.isoformat(),
                    "mode": "Ambient_Mode",
                    "result_text": f"Scent {spray_code} / Spike: {is_spike}",
                    "spray_code": int(spray_code),
                    "duration": int(duration),
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
                "cmd": int(spray_code),
                "spray": int(spray_code),
                "duration": int(duration),
                "music": int(music_code),
                "led_r": int(led_dict.get("led_r", 255)),
                "led_g": int(led_dict.get("led_g", 180)),
                "led_b": int(led_dict.get("led_b", 0)),
                "led_bright": int(led_dict.get("led_bright", 150))
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
