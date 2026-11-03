# 향기 추천에 대한 사용자 피드백(좋아요/싫어요)을 DB에 반영하는 핸들러임

import os
import json
import logging
import math
import random
from datetime import datetime, timedelta, timezone

import config
import db_utils
import api_utils
from .common import (
    _parse_int,
    _get_ambient_scent_ids,
    _build_ambient_context,
    _resolve_ambient_effective_db,
    build_weather_context_key,
)

logger = logging.getLogger()


def _replace_diary_feedback(device_id, context, spray_no, rejected_kind, email):
    """Queue a complete replacement after the diary app's explicit stop."""
    state = db_utils.state_table.get_item(
        Key={"deviceId": device_id}, ConsistentRead=True,
    ).get("Item", {})
    mapping = db_utils.get_spray_mapping(device_id)
    disabled = {int(slot) for slot in getattr(config, "DIARY_DISABLED_SPRAY_SLOTS", (2,))}
    rejected = {int(digit) for digit in str(rejected_kind) if digit.isdigit()}
    weights = state.get("weights")
    candidates = []
    for slot, kind in sorted((int(slot), int(kind)) for slot, kind in mapping.items()):
        if slot not in (1, 2, 3, 4) or slot in disabled or kind in rejected or kind not in config.SCENT_KINDS:
            continue
        if isinstance(weights, list):
            try:
                percent = float(weights[slot - 1])
            except (IndexError, TypeError, ValueError):
                continue
            if not math.isfinite(percent) or percent < config.AMBIENT_MIN_SCENT_PERCENT:
                continue
        music = int(db_utils.get_music_track(device_id, slot, is_pump=True) or 0)
        candidates.append((kind, slot, music))

    if not candidates:
        return {"statusCode": 200, "body": json.dumps({
            "result": "FAIL", "message": "대체할 수 있는 정상 장착 향기가 없습니다.",
        }, ensure_ascii=False)}

    old_music = int(state.get("current_music", 0) or 0)
    if old_music <= 0:
        old_music = int(db_utils.get_music_track(device_id, spray_no, is_pump=True) or 0)
    different_music = [item for item in candidates if item[2] > 0 and item[2] != old_music]
    pool = different_music or candidates
    new_kind = db_utils.get_preferred_alternative(
        device_id, context, rejected_kind, email,
        allowed_kinds=[item[0] for item in pool],
    )
    new_kind, new_spray, music = next((item for item in pool if item[0] == new_kind), pool[0])
    duration = 3
    saved = db_utils.manage_mailbox(
        device_id, new_spray, context, duration,
        save_active_mode="manual", save_active_scent=new_spray, save_music=music,
        save_diary_music=music,
        expected_initial_state=state,
    )
    if saved != new_spray:
        return {"statusCode": 200, "body": json.dumps({
            "result": "FAIL", "message": "새 정지 또는 모드 변경으로 대체 명령을 적용하지 않았습니다.",
        }, ensure_ascii=False)}

    scheduled = db_utils.schedule_implicit_like(
        device_id, context, new_kind, email, delay_seconds=600,
    )
    return {
        "statusCode": 200, "headers": {"Content-Type": "application/json"},
        "body": json.dumps({
            "result": "SUCCESS", "status": "SUCCESS",
            "message": f"싫어요를 반영해 {new_spray}번 향기와 음악 변경 명령을 보냈습니다.",
            "spray": new_spray, "active_mode": "manual", "active_scent": new_spray,
            "app_command": True, "duration": duration, "music": music, "context": context,
            "implicit_like_scheduled": scheduled,
            "implicit_like_after_seconds": 600 if scheduled else 0,
        }, ensure_ascii=False),
    }

# handle_feedback 처리를 담당함
def handle_feedback(action, mode, body, device_id):
    if action in ["FEEDBACK", "LIKE", "DISLIKE"]:
        raw_value = body.get("value", 1)
        try:
            numeric_value = int(raw_value)
        except Exception:
            normalized_value = str(raw_value).strip().lower()
            numeric_value = 1 if normalized_value in ["1", "good", "like", "true"] else -1

        feedback_action = str(body.get("feedback_action") or action).strip().upper()
        if feedback_action == "LIKE":
            is_like = True
        elif feedback_action == "DISLIKE":
            is_like = False
        else:
            is_like = numeric_value > 0

        ambient_context = str(body.get("ambient_context", "") or "").strip()
        feedback_context = str(body.get("context", "") or "").strip()
        feedback_data = str(body.get("data", "") or "").strip()
        feedback_data_context = ""
        if feedback_data:
            feedback_parts = feedback_data.split("_", 1)
            feedback_data_context = feedback_parts[1] if len(feedback_parts) > 1 else feedback_data

        # 명시적인 새 피드백은 이전 대체 향의 자동 +1 예약을 항상 취소함
        db_utils.cancel_implicit_like(device_id)

        if not ambient_context and feedback_context.startswith("Ambient_"):
            ambient_context = feedback_context[len("Ambient_"):]

        if not ambient_context and feedback_data:
            if feedback_data_context.startswith("Ambient_"):
                ambient_context = feedback_data_context[len("Ambient_"):]

        if not ambient_context and mode == "ambient":
            if feedback_context:
                ambient_context = feedback_context
            elif feedback_data:
                feedback_parts = feedback_data.split("_", 1)
                ambient_context = feedback_parts[1] if len(feedback_parts) > 1 else feedback_data

        # The current app labels noise feedback as NOISE_ANALYSIS and sends it
        # with mode=manual. Recover the canonical ambient context from the
        # device state so the feedback updates AmbientWeights.
        is_noise_feedback = (
            feedback_context == "NOISE_ANALYSIS"
            or feedback_data_context == "NOISE_ANALYSIS"
            or ambient_context == "NOISE_ANALYSIS"
        )
        if is_noise_feedback and ambient_context == "NOISE_ANALYSIS":
            ambient_context = ""
        if not ambient_context and is_noise_feedback:
            ambient_state = db_utils.get_device_state(device_id) or {}
            ambient_context = str(ambient_state.get("ambient_context", "") or "").strip()
            if not ambient_context:
                effective_db = _resolve_ambient_effective_db({}, ambient_state)
                ambient_context, _, _, _ = _build_ambient_context(effective_db)

        if ambient_context.startswith("Ambient_"):
            ambient_context = ambient_context[len("Ambient_"):]

        if ambient_context:
            try:
                ambient_scent_ids = _get_ambient_scent_ids()
                spray_no = _parse_int(body.get("current_scent_id"), 0)
                if spray_no <= 0:
                    spray_no = _parse_int(body.get("scent"), 0)
                if spray_no <= 0 and feedback_data:
                    feedback_parts = feedback_data.split("_", 1)
                    if feedback_parts and feedback_parts[0].isdigit():
                        spray_no = int(feedback_parts[0])

                scent_kind = db_utils.get_kind_from_spray(device_id, spray_no) or spray_no
                if scent_kind not in ambient_scent_ids:
                    scent_kind = int(config.AMBIENT_DEFAULT_SCENT)

                magnitude = abs(numeric_value) or 1
                weight_val = magnitude if is_like else -magnitude
                db_utils.seed_ambient_defaults()
                db_utils.save_ambient_feedback(ambient_context, scent_kind, weight_val)

                response_spray = spray_no
                implicit_like_scheduled = False
                if not is_like:
                    ambient_state = db_utils.get_device_state(device_id) or {}
                    if ambient_state.get("active_mode") != "ambient" or ambient_state.get("ambient_startup_pending", False):
                        return {"statusCode": 200, "body": json.dumps({
                            "result": "SUCCESS", "message": "Feedback saved without starting a spray",
                            "spray": -1, "app_command": False,
                        })}
                    replacement_kind = db_utils.get_ambient_alternative(
                        ambient_context,
                        device_id,
                        scent_kind,
                    )
                    response_spray = db_utils.get_spray_from_kind(device_id, replacement_kind) or 1
                    saved_command = db_utils.manage_mailbox(
                        device_id,
                        response_spray,
                        ambient_context,
                        config.AMBIENT_DURATION_SECONDS,
                        save_active_mode="ambient",
                        save_active_scent=response_spray,
                        save_ambient_lease_duration=config.AMBIENT_DURATION_SECONDS,
                        expected_initial_state=ambient_state,
                    )
                    if saved_command != response_spray:
                        return {"statusCode": 200, "body": json.dumps({
                            "result": "SUCCESS", "message": "Feedback spray superseded",
                            "spray": -1, "app_command": False,
                        })}
                    implicit_like_scheduled = db_utils.schedule_implicit_like(
                        device_id,
                        ambient_context,
                        replacement_kind,
                        body.get("email"),
                        delay_seconds=600,
                        feedback_store="ambient",
                    )

                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "result": "SUCCESS",
                        "status": "SUCCESS",
                        "message": "Ambient feedback saved",
                        "spray": response_spray,
                        "cmd": response_spray if not is_like else 0,
                        "active_mode": "ambient",
                        "active_scent": response_spray if not is_like else spray_no,
                        "context": f"Ambient_{ambient_context}",
                        "ambient_context": ambient_context,
                        "value": weight_val,
                        "implicit_like_scheduled": implicit_like_scheduled,
                        "implicit_like_after_seconds": 600 if implicit_like_scheduled else 0
                    }, ensure_ascii=False)
                }
            except Exception as e:
                return {"statusCode": 500, "body": str(e)}

        try:
            data_str = str(body.get("data") or "").strip()
            parts = data_str.split("_", 1)

            spray_no = _parse_int(body.get("current_scent_id"), 0)
            if spray_no <= 0:
                spray_no = int(parts[0]) if parts and parts[0].isdigit() else 1
            real_kind_code = db_utils.get_kind_from_spray(device_id, spray_no) or spray_no
            ctx = feedback_context or (parts[1] if len(parts) > 1 else "Unknown")
            
            if ctx == "AI_WEATHER":
                try:
                    region = body.get("region")
                    if not region or "_" in str(region):
                        region = db_utils.get_saved_region(device_id) or "서울"
                    
                    coord = config.REGION_COORDS.get(region, config.REGION_COORDS["서울"])
                    nx, ny = coord["nx"], coord["ny"]
                    
                    bd, bt = api_utils.get_kma_time()
                    _, w_res, _ = api_utils.forecast({
                        "serviceKey": os.environ.get("SERVICE_KEY", ""),
                        "pageNo": "1",
                        "numOfRows": "60",
                        "dataType": "XML",
                        "base_date": bd,
                        "base_time": bt,
                        "nx": nx,
                        "ny": ny,
                    })
                    ctx = build_weather_context_key(w_res)
                    logger.info(f"[FEEDBACK_CTX] Created context: {ctx} for region: {region}")
                except Exception as e:
                    logger.error(f"[FEEDBACK_CTX_ERR] {e}")
                    ctx = "Weather_Unknown"

            if "새벽" not in ctx and "오전" not in ctx and "오후" not in ctx and "밤" not in ctx:
                now_kst = datetime.now(timezone(timedelta(hours=9)))
                h = now_kst.hour
                
                if 0 <= h < 6:
                    t_zone = "새벽"
                elif 6 <= h < 12:
                    t_zone = "오전"
                elif 12 <= h < 18:
                    t_zone = "오후"
                else:
                    t_zone = "밤"
                
                if ctx.startswith("Weather_"):
                    ctx = f"{ctx}_{t_zone}"

            magnitude = abs(numeric_value) or 1
            weight_val = magnitude if is_like else -magnitude
            u_email = body.get("email")
            db_utils.save_feedback(device_id, ctx, real_kind_code, weight_val, u_email)

            if not is_like:
                if ctx == "AI_DIARY":
                    return _replace_diary_feedback(device_id, ctx, spray_no, real_kind_code, u_email)
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression="set last_spray_time = :t",
                    ExpressionAttributeValues={':t': "2000-01-01T00:00:00"}
                )
                
                new_kind = db_utils.get_preferred_alternative(
                    device_id,
                    ctx,
                    real_kind_code,
                    u_email,
                )
                if new_kind is None:
                    candidates = [kind for kind in db_utils._kind_ids() if kind != real_kind_code]
                    new_kind = random.choice(candidates)
                new_spray = db_utils.get_spray_from_kind(device_id, new_kind) or 1
                db_utils.manage_mailbox(device_id, new_spray)
                implicit_like_scheduled = db_utils.schedule_implicit_like(
                    device_id,
                    ctx,
                    new_kind,
                    u_email,
                    delay_seconds=600,
                )
                
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "message": f"싫어요 반영! {new_spray}번 칸 향기로 대체 분사",
                        "spray": new_spray,
                        "context": ctx,
                        "implicit_like_scheduled": implicit_like_scheduled,
                        "implicit_like_after_seconds": 600
                    }, ensure_ascii=False)
                }

            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "message": "좋아요! 사용자 선호도 학습 완료", 
                    "spray": spray_no,
                    "context": ctx
                }, ensure_ascii=False)
            }

        except Exception as e:
             logger.error(f"[FEEDBACK_FATAL] {e}")
             return {"statusCode": 200, "body": json.dumps({"result": "FAIL", "message": f"피드백 처리 실패: {str(e)}"}, ensure_ascii=False)}

    elif action == "REPORT":
        try:
            target_email = body.get("email") or db_utils._get_email_by_device_id(device_id)
            if not target_email:
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "result": "FAIL",
                        "message": "사용자 계정을 찾을 수 없어 분석할 수 없습니다."
                    }, ensure_ascii=False)
                }
            
            resp = db_utils.pref_table.query(
                KeyConditionExpression=db_utils.Key('email').eq(str(target_email))
            )
            items = resp.get("Items", [])
            
            if not items:
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "result": "SUCCESS",
                        "summary": "아직 충분한 취향 데이터가 모이지 않았어요. 디퓨저를 더 많이 사용하고 피드백을 남겨주세요!",
                        "total_analyzed": 0,
                        "top_scent": "정보 없음",
                        "top_context": "정보 없음"
                    }, ensure_ascii=False)
                }

            scent_names = config.SCENT_KINDS

            best_scent_id = -1
            best_scent_score = -9999
            best_context = ""
            
            total_feedbacks = 0
            scent_totals = {kind: 0 for kind in db_utils._kind_ids()}

            for item in items:
                context = item.get("context", "")
                for kind in db_utils._kind_ids():
                    score = int(item.get(f"scent_{kind}_score", 0))
                    if score != 0:
                        total_feedbacks += abs(score)
                        scent_totals[kind] += score
                        
                        if score > best_scent_score:
                            best_scent_score = score
                            best_scent_id = kind
                            best_context = context

            if total_feedbacks == 0 or best_scent_score <= 0:
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "result": "SUCCESS",
                        "summary": "긍정적인 피드백 데이터가 부족합니다. 좋아하는 향기에 '좋아요'를 눌러주시면 취향을 분석해 드릴게요!",
                        "total_analyzed": total_feedbacks,
                        "top_scent": "정보 없음",
                        "top_context": "정보 없음"
                    }, ensure_ascii=False)
                }

            exact_context_map = {
                "AI_DIARY": "AI 감정 일기에서",
                "AI_EMOTION": "AI 감정 모드에서",
                "AI_VOICE": "AI 음성 모드에서",
                "NOISE_ANALYSIS": "소음 분석 모드에서",
            }
            context_map = {
                "Rain": "비오는 날",
                "Sunny": "맑은 날",
                "Snow": "눈오는 날",
                "Cloudy": "흐린 날",
                "새벽": "새벽에",
                "오전": "오전에",
                "오후": "오후에",
                "밤": "밤에",
                "Emotion_Happy": "기분이 좋을 때",
                "Emotion_Sad": "우울할 때",
                "Emotion_Tired": "피곤할 때",
                "Emotion_Angry": "화날 때"
            }
            
            translated_ctx = exact_context_map.get(best_context, best_context)
            for key, val in context_map.items():
                if key in translated_ctx:
                    translated_ctx = translated_ctx.replace(f"Weather_{key}", val)
                    translated_ctx = translated_ctx.replace(f"_{key}", f" {val}")
                    translated_ctx = translated_ctx.replace(key, val)

            top_scent_name = scent_names.get(best_scent_id, f"{best_scent_id}번 향기")
            
            summary_msg = f"당신은 주로 {translated_ctx} {top_scent_name}을(를) 가장 좋아하시네요!"

            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({
                    "result": "SUCCESS",
                    "summary": summary_msg,
                    "total_analyzed": total_feedbacks,
                    "top_scent": top_scent_name,
                    "top_context": translated_ctx
                }, ensure_ascii=False)
            }
            
        except Exception as e:
            logger.error(f"[REPORT_FATAL] {e}")
            return {"statusCode": 200, "body": json.dumps({"result": "FAIL", "message": f"분석 중 오류 발생: {str(e)}"}, ensure_ascii=False)}

    return None
