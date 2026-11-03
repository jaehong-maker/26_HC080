# 날씨 동기화, 감정 일기 분석 등 디퓨저의 핵심 구동 모드를 처리하는 핸들러임

import os
import json
import logging
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import time
import config
import db_utils
import api_utils
from .common import build_weather_context_key, logic_weather_mode, logic_emotion_mode
logger = logging.getLogger()


def _kind_digits(kind_code):
    return [int(value) for value in str(kind_code or "") if value.isdigit() and int(value) > 0]


def _get_diary_safe_scent_policy(device_id):
    """일기 모드에서 고장 카트리지를 제외한 향기와 펌프 매핑을 반환함."""
    disabled_sprays = {
        int(spray)
        for spray in getattr(config, "DIARY_DISABLED_SPRAY_SLOTS", (2,))
    }
    mapping = {
        int(spray): int(kind)
        for spray, kind in db_utils.get_spray_mapping(device_id).items()
    }
    safe_kinds = []
    for spray, kind in sorted(mapping.items()):
        if spray in disabled_sprays or kind not in getattr(config, "SCENT_KINDS", {}):
            continue
        if kind not in safe_kinds:
            safe_kinds.append(kind)
    return mapping, disabled_sprays, safe_kinds


def _is_diary_kind_allowed(kind_code, allowed_kinds):
    requested_kinds = _kind_digits(kind_code)
    allowed = set(int(kind) for kind in allowed_kinds)
    return bool(requested_kinds) and all(kind in allowed for kind in requested_kinds)


def _map_diary_kind_to_safe_pumps(kind_code, mapping, disabled_sprays):
    pumps = []
    for target_kind in _kind_digits(kind_code):
        matching_pump = next(
            (
                int(spray)
                for spray, kind in sorted(mapping.items())
                if int(spray) not in disabled_sprays and int(kind) == target_kind
            ),
            None,
        )
        if matching_pump is None:
            return 0
        if matching_pump not in pumps:
            pumps.append(matching_pump)
    # The device accepts canonical pairs (12, 13, 14, 23, 24, 34).
    # AI kind order must not produce reversed commands such as 41 or 43.
    return int("".join(str(pump) for pump in sorted(pumps))) if pumps else 0


# 날씨, 감정 등 메인 모드의 로직을 수행함
def handle_core_modes(action, mode, body, device_id, normalized_weights, region):
    weight_g = normalized_weights[0]
    logger.info(f"[WEIGHT_SYNC] 정규화된 무게 배열: {normalized_weights} | 대표 무게: {weight_g}g")
    spray_code = 0
    result_text = ""
    logic_name = ""
    temp = "0"
    humidity = "0"
    duration = 3
    context_key = "Unknown"
    weather_text = ""
    emotion_summary = ""
    diary_mapping = {}
    diary_disabled_sprays = set()
    diary_allowed_kinds = []
    if mode == "emotion":
        user_emo_tag = body.get("user_emotion", "평범")
        diary_full_text = body.get("diary_text", user_emo_tag) 
        context_key = "AI_DIARY" if body.get("diary_text") else "AI_EMOTION"
        u_email = body.get("email")
        user_history_str = db_utils.get_user_history_text(
            device_id,
            u_email,
            contexts=[context_key],
        )
        if context_key == "AI_DIARY":
            diary_mapping, diary_disabled_sprays, diary_allowed_kinds = (
                _get_diary_safe_scent_policy(device_id)
            )
            active_kinds = diary_allowed_kinds
            if not active_kinds:
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "result": "FAIL",
                        "spray": 0,
                        "message": "2번 카트리지를 제외하면 사용할 수 있는 향기가 없습니다."
                    }, ensure_ascii=False)
                }
        else:
            active_kinds = db_utils._active_kinds(device_id)
        kind_descriptions = {
            1: "1번(시트러스: 상쾌/에너지/우울감 극복)",
            2: "2번(센달우드: 차분/집중/안정)",
            3: "3번(패츄리: 자연/깊은휴식/명상)",
            4: "4번(페퍼민트: 시원/피로해소/답답함 해소)",
            5: "5번(라벤더: 진정/수면/스트레스 완화)",
            6: "6번(바닐라: 포근/위로/외로움 해소)",
            7: "7번(무향)"
        }
        if active_kinds:
            active_kinds_str = "\n".join([kind_descriptions.get(int(k), f"{k}번(알 수 없는 향기)") for k in active_kinds])
        else:
            active_kinds_str = "장착된 향기가 없습니다."
        gemini_kind_str, ai_reason, led_dict, emotion_summary, emotion_tag = api_utils.ask_gemini_recommendation(
            diary_text=diary_full_text, 
            user_history_text=user_history_str,
            slot_info_str=active_kinds_str
        )
        if emotion_tag and emotion_tag != "에러":
            user_emo_tag = emotion_tag
        if led_dict and led_dict.get("led_r", -1) >= 0:
            try:
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression="set led_r = :r, led_g = :g, led_b = :b, led_br = :br",
                    ExpressionAttributeValues={
                        ':r': led_dict["led_r"], ':g': led_dict["led_g"], ':b': led_dict["led_b"], ':br': led_dict["led_bright"]
                    }
                )
            except Exception as e:
                logger.error(f"Emotion LED DB 업데이트 실패: {e}")
        try:
            kind_code = int(gemini_kind_str) if gemini_kind_str != "0" else 0
        except Exception:
            kind_code = 0
        if kind_code > 0 and context_key == "AI_DIARY" and not _is_diary_kind_allowed(
            kind_code,
            diary_allowed_kinds,
        ):
            original_kind_code = kind_code
            kind_code = int(diary_allowed_kinds[0])
            ai_reason = (
                f"{ai_reason} 2번 카트리지 보호를 위해 "
                f"다른 장착 향기({kind_code}번)로 대체했습니다."
            )
            logger.warning(
                f"[DIARY_CARTRIDGE_GUARD_AI] ID:{device_id} "
                f"Kind:{original_kind_code}->{kind_code} DisabledPumps:{sorted(diary_disabled_sprays)}"
            )
        if kind_code == 0:
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "result": "FAIL",
                    "message": f"AI 서버 과부하. 잠시 후 다시 시도해주세요. ({ai_reason})"
                }, ensure_ascii=False)
            }
        if body.get("diary_text") and body.get("email"):
            db_utils.save_diary(body["email"], body["diary_text"], user_emo_tag, ai_reason)
        final_ai_msg = f"오늘의 감정: {emotion_summary}\n\n{ai_reason}" if emotion_summary else ai_reason
        spray_code, result_text, logic_name = kind_code, final_ai_msg, "AI_Gemini_Emotion"
        duration = 3
    elif mode == "ai_replay":
        spray_code = 1
        result_text = "이전 AI 추천 기록이 없어 기본 향기로 분사합니다."
        logic_name = "AI_Replay"
        duration = 3
        context_key = "AI_EMOTION"
        try:
            from boto3.dynamodb.conditions import Key
            log_resp = db_utils.log_table.query(
                KeyConditionExpression=Key("deviceId").eq(device_id),
                ScanIndexForward=False,
                Limit=30
            )
            for item in log_resp.get("Items", []):
                if item.get("mode") == "AI_Gemini_Emotion":
                    replayed_pump = int(item.get("spray_code", 1))
                    spray_code = db_utils.get_kind_from_spray(device_id, replayed_pump) or replayed_pump
                    result_text = "가장 최근의 AI 추천 설정(향기, 조명, 음악)으로 즉시 분사를 시작합니다."
                    break
        except Exception as e:
            logger.error(f"[AI_REPLAY_ERROR] {e}")
    else:
        try: 
            if region == "테스트맑음":
                spray_code, result_text, logic_name, duration = 1, "테스트 맑음 실행", "Test_Weather", 3
                context_key = build_weather_context_key("맑음")
            elif region == "테스트흐림":
                spray_code, result_text, logic_name, duration = 2, "테스트 흐림 실행", "Test_Weather", 3
                context_key = build_weather_context_key("흐림")
            elif region == "테스트비":
                spray_code, result_text, logic_name, duration = 3, "테스트 비 실행", "Test_Weather", 3
                context_key = build_weather_context_key("비")
            elif region == "테스트눈":
                spray_code, result_text, logic_name, duration = 4, "테스트 눈 실행", "Test_Weather", 3
                context_key = build_weather_context_key("눈")
            else:
                explicit_region = str(
                    body.get("region") or body.get("data") or ""
                ).strip()
                if explicit_region:
                    region = explicit_region
                    logger.info(f"[WEATHER_REGION] Explicit request region selected: {region}")
                else:
                    # 앱에서 변경하여 사용자 정보에 저장된 지역을 최우선으로 사용함.
                    # 앱 저장 지역이 없을 때만 최근 응답 지역을 대체값으로 사용함.
                    saved_region = db_utils.get_saved_region(device_id)
                    if saved_region and saved_region in config.REGION_COORDS:
                        region = saved_region
                        logger.info(f"[WEATHER_REGION] App saved region selected: {region}")
                    else:
                        voice_region = db_utils.get_voice_region(device_id)
                        if voice_region and voice_region in config.REGION_COORDS:
                            region = voice_region
                            logger.info(f"[WEATHER_REGION] Recent response region selected: {region}")
                if not region: region = "서울"
                if region not in config.REGION_COORDS: region = "서울"
                db_utils.remember_last_region(device_id, region)
                nx, ny = config.REGION_COORDS[region]["nx"], config.REGION_COORDS[region]["ny"]
                if "SERVICE_KEY" in os.environ:
                    bd, bt = api_utils.get_kma_time()
                    params = {
                        "serviceKey": os.environ["SERVICE_KEY"],
                        "pageNo": "1", "numOfRows": "60", "dataType": "XML",
                        "base_date": bd, "base_time": bt, "nx": nx, "ny": ny
                    }
                    temp, w_res, humidity = api_utils.forecast(params)
                    logger.info(f"[KMA_RESULT] Temp:{temp}, Weather:{w_res}, Humid:{humidity}")
                    if not w_res or w_res in ["0", "통신에러", "에러"]:
                        logger.warning(f"[WEATHER_FALLBACK] API error or empty. Using default Sunny logic. (w_res: {w_res})")
                        w_res = "맑음(API지연)"
                        temp, humidity = "20", "50"
                    weather_text = w_res
                    context_key = build_weather_context_key(w_res)
                    spray_code, result_text, logic_name, duration = logic_weather_mode(w_res, humidity)
                else:
                    spray_code, result_text, logic_name, duration = 1, "API키없음", "Error", 3
                    weather_text = "환경 변수 오류"
        except Exception as e:
            logger.error(f"[WEATHER_MODE_CRASH] {e}")
            spray_code, result_text, logic_name, duration = 1, f"날씨 처리 에러: {str(e)}", "Error_Mode", 3
            weather_text = "날씨 통신 실패"
            temp, humidity = "0", "0"
            context_key = "Weather_Unknown"   
    if spray_code > 0:
        u_email = body.get("email")
        final_scent, ai_msg = db_utils.get_personalized_scent(device_id, context_key, spray_code, u_email)
        if context_key == "AI_DIARY" and not _is_diary_kind_allowed(
            final_scent,
            diary_allowed_kinds,
        ):
            blocked_scent = final_scent
            final_scent = int(diary_allowed_kinds[0])
            ai_msg = "2번 카트리지 보호 대체"
            logger.warning(
                f"[DIARY_CARTRIDGE_GUARD_PERSONALIZATION] ID:{device_id} "
                f"Kind:{blocked_scent}->{final_scent}"
            )
        if final_scent != spray_code:
            logger.info(f"[AI Change] {spray_code} -> {final_scent} ({ai_msg})")
            result_text += f" -> [취향 맞춤: {final_scent}번]"
            spray_code = final_scent
        if context_key == "AI_DIARY":
            mapped_pump = _map_diary_kind_to_safe_pumps(
                spray_code,
                diary_mapping,
                diary_disabled_sprays,
            )
            fb_msg = ""
            if mapped_pump <= 0:
                safe_kind = int(diary_allowed_kinds[0])
                mapped_pump = _map_diary_kind_to_safe_pumps(
                    safe_kind,
                    diary_mapping,
                    diary_disabled_sprays,
                )
                result_text += f" -> [2번 카트리지 제외: {safe_kind}번 향기로 대체]"
            logger.info(
                f"[DIARY_SAFE_PUMP] ID:{device_id} Kind:{spray_code} "
                f"Pump:{mapped_pump} Disabled:{sorted(diary_disabled_sprays)}"
            )
        else:
            mapped_pump, fb_msg = db_utils.get_smart_spray_mapping(device_id, spray_code)
        result_text += fb_msg
        spray_code = mapped_pump
    if spray_code > 0 and action != "START" and "_PREVIEW" not in action and context_key != "AI_DIARY":
        logger.info(f"[MAILBOX_PRE_WRITE] ID:{device_id} Scent:{spray_code} Mode:{mode} Action:{action}")
        db_utils.manage_mailbox(device_id, spray_code, region if mode == "weather" else "Emotion", duration)
    state = db_utils.get_device_state(device_id) or {}
    intensity = int(state.get('intensity', 2))
    last_time_str = state.get('last_spray_time', "2000-01-01T00:00:00")
    current_capacity = float(state.get('current_capacity', config.MAX_CAPACITY))
    now = datetime.now(timezone(timedelta(hours=9)))
    is_blocked = False
    log_msg = ""
    if spray_code > 0 and action != "START" and "_PREVIEW" not in action:
        try:
            last_time = datetime.fromisoformat(last_time_str)
            if (now - last_time).total_seconds() < (config.COOLDOWN_MINUTES * 60):
                is_blocked = True
                remaining_sec = int((config.COOLDOWN_MINUTES * 60) - (now - last_time).total_seconds())
                log_msg = f"쿨타임 중: {remaining_sec}초 남음"
        except: pass
    if is_blocked:
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({
                "spray": 0,
                "weather": weather_text,
                "region": region,
                "target_region": region,
                "temp": temp,
                "temperature": temp,
                "humidity": humidity,
                "humid": humidity,
                "result_text": result_text, 
                "message": log_msg,
                "mode": "CoolDown"
            }, ensure_ascii=False)
        }
    new_capacity = current_capacity
    if spray_code > 0 and "_PREVIEW" not in action:
        if action == "START":
            start_code = int(body.get("spray", 30))
            db_utils.manage_mailbox(device_id, start_code)
        else:
            usage = duration * config.CONSUMPTION_PER_SEC
            new_capacity = round(max(0.0, current_capacity - usage), 2)
            db_utils.update_device_state(device_id, now.isoformat(), new_capacity, spray_code, normalized_weights)
    if "_PREVIEW" in action:
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({
                "success": True,
                "weather": weather_text,
                "region": region,
                "target_region": region,
                "spray": 0,
                "intensity": intensity,
                "result_text": f"미리보기: {result_text}",
                "duration": duration,
                "temp": temp,
                "temperature": temp,
                "humidity": humidity,
                "humid": humidity,
                "weights": normalized_weights,
                "message": f"날씨 분석 성공: {weather_text}",
                "mode": logic_name,
                "context": context_key  
            }, ensure_ascii=False)
        }
    db_log_item = None
    try:
        def safe_int(v): return int(float(v)) if v else 0
        def safe_dec(v): return Decimal(str(v)) if v else Decimal("0")
        db_weights = state.get('weights', [0.0, 0.0, 0.0, 0.0])
        true_weights = [float(w) for w in db_weights] if isinstance(db_weights, list) else [0.0, 0.0, 0.0, 0.0]
        log_weights = normalized_weights if sum(normalized_weights) > 0 else true_weights

        db_log_item = {
            "deviceId": device_id,
            "timestamp": now.isoformat(),
            "mode": logic_name,
            "result_text": result_text,
            "spray_code": int(spray_code),
            "duration": int(duration),
            "region": region if mode == "weather" else "Emotion",
            "weight_g": safe_dec(weight_g),
            "weights": [safe_dec(weight) for weight in log_weights],
            "temp": safe_dec(temp),
            "humid": safe_int(humidity),
            "feedback": 1
        }
        db_utils.log_table.put_item(Item=db_log_item)
        db_log_item["weight_g"] = float(db_log_item["weight_g"])
        db_log_item["weights"] = [float(w) for w in db_log_item["weights"]]
        db_log_item["temp"] = float(db_log_item["temp"])
    except Exception as e:
        logger.warning(f"Log save failed: {e}")
    intensity = int(state.get('intensity', 2))
    led_r = int(state.get('led_r', 255))
    led_g = int(state.get('led_g', 255))
    led_b = int(state.get('led_b', 255))
    led_br = int(state.get('led_br', 150))
    if mode == "emotion" and 'led_dict' in locals() and led_dict and led_dict.get("led_r", -1) >= 0:
        led_r = led_dict["led_r"]
        led_g = led_dict["led_g"]
        led_b = led_dict["led_b"]
        led_br = led_dict["led_bright"]

    if mode == "weather" and action != "START" and "_PREVIEW" not in action:
        try:
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression="set active_mode = :am, active_scent = :asc, weather = :w, #tmp = :t, humidity = :h, target_region = :r, last_mode_change_time = :lmc",
                ExpressionAttributeNames={
                    '#tmp': 'temp'
                },
                ExpressionAttributeValues={
                    ':am': 'weather',
                    ':asc': int(spray_code),
                    ':w': weather_text,
                    ':t': str(temp),
                    ':h': str(humidity),
                    ':r': region,
                    ':lmc': int(time.time())
                }
            )
        except Exception as e:
            logger.error(f"Failed to save weather state to DB: {e}")
    elif mode in ["emotion", "ai_replay"] and spray_code > 0 and action != "START" and "_PREVIEW" not in action:
        if context_key == "AI_DIARY":
            music = db_utils.get_music_track(device_id, spray_code, is_pump=True)
            snapshot = db_utils.state_table.get_item(
                Key={"deviceId": device_id}, ConsistentRead=True,
            ).get("Item", {})
            saved = db_utils.manage_mailbox(
                device_id, 0, "AI_DIARY", 0,
                save_active_mode="ready", save_active_scent=0, save_music=0,
                save_diary_start={"spray": spray_code, "music": music, "duration": duration},
                expected_initial_state=snapshot,
            )
            if saved != 0:
                return {"statusCode": 200, "body": json.dumps({
                    "result": "FAIL", "message": "일기 음악 재생 명령을 저장하지 못했습니다.",
                }, ensure_ascii=False)}
        else:
            try:
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression="set active_mode = :am, active_scent = :asc, current_spray = :asc, pending_cmd = :asc, last_mode_change_time = :lmc REMOVE auto_stop_time",
                    ExpressionAttributeValues={
                        ':am': 'manual',
                        ':asc': int(spray_code),
                        ':lmc': int(time.time())
                    }
                )
            except Exception as e:
                logger.error(f"Failed to save emotion state to DB: {e}")

    music_code = 0
    if spray_code > 0:
        music_code = db_utils.get_music_track(device_id, spray_code, is_pump=True)

    # true_weights는 위에서 계산됨
    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({
            "active_mode": "weather" if mode == "weather" else mode,
            "weather": weather_text,
            "region": region,
            "target_region": region,
            "spray": spray_code,
            "music": music_code,
            "intensity": intensity,
            "result_text": result_text,
            "duration": duration,
            "temp": temp,
            "temperature": temp,
            "humidity": humidity,
            "humid": humidity,
            "weight_g": true_weights[0],
            "weights": true_weights,
            "remaining_capacity": new_capacity,
            "message": result_text,
            "mode": logic_name,
            "context": context_key,
            "emotion_summary": emotion_summary if emotion_summary else "",
            "emotion_tag": emotion_tag if 'emotion_tag' in locals() else "",
            "led_r": led_r,
            "led_g": led_g,
            "led_b": led_b,
            "led_bright": led_br,
            "db_log_preview": db_log_item
        }, ensure_ascii=False)
    }
