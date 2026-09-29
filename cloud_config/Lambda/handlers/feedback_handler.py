# 향기 추천에 대한 사용자 피드백(좋아요/싫어요)을 DB에 반영하는 핸들러임

import os
import json
import logging
import random
from datetime import datetime, timedelta, timezone

import config
import db_utils
import api_utils
from .common import _parse_int, _get_ambient_scent_ids

logger = logging.getLogger()

# handle_feedback 처리를 담당함
def handle_feedback(action, mode, body, device_id):
    if action == "FEEDBACK":
        ambient_context = str(body.get("ambient_context", "") or "").strip()
        feedback_context = str(body.get("context", "") or "").strip()
        feedback_data = str(body.get("data", "") or "").strip()

        if not ambient_context and feedback_context.startswith("Ambient_"):
            ambient_context = feedback_context[len("Ambient_"):]

        if not ambient_context and mode == "ambient":
            if feedback_context:
                ambient_context = feedback_context
            elif feedback_data:
                feedback_parts = feedback_data.split("_", 1)
                ambient_context = feedback_parts[1] if len(feedback_parts) > 1 else feedback_data

        if ambient_context.startswith("Ambient_"):
            ambient_context = ambient_context[len("Ambient_"):]

        if ambient_context:
            try:
                ambient_scent_ids = _get_ambient_scent_ids()
                scent = _parse_int(body.get("scent"), 0)
                if scent not in ambient_scent_ids and feedback_data:
                    feedback_parts = feedback_data.split("_", 1)
                    if feedback_parts and feedback_parts[0].isdigit():
                        scent = int(feedback_parts[0])
                if scent not in ambient_scent_ids:
                    scent = int(config.AMBIENT_DEFAULT_SCENT)

                raw_value = body.get("value", 1)
                try:
                    val = int(raw_value)
                except Exception:
                    normalized_value = str(raw_value).strip().lower()
                    val = 1 if normalized_value in ["1", "good", "like", "true"] else -1

                weight_val = 1 if val == 1 else -1
                db_utils.seed_ambient_defaults()
                db_utils.save_ambient_feedback(ambient_context, scent, weight_val)
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "message": "Ambient feedback saved",
                        "spray": 0,
                        "cmd": 0,
                        "context": f"Ambient_{ambient_context}",
                        "ambient_context": ambient_context,
                        "value": weight_val
                    }, ensure_ascii=False)
                }
            except Exception as e:
                return {"statusCode": 500, "body": str(e)}

        try:
            val = body.get("value", 1) 
            is_like = (val == 1)

            data_str = body.get("data", "1_Unknown")
            parts = data_str.split("_", 1)

            spray_no = int(parts[0]) if parts[0].isdigit() else 1
            real_kind_code = db_utils.get_kind_from_spray(device_id, spray_no) or spray_no
            ctx = parts[1] if len(parts) > 1 else "Unknown"
            
            if ctx == "AI_WEATHER":
                try:
                    region = body.get("region")
                    if not region or "_" in str(region):
                        region = db_utils.get_saved_region(device_id) or "서울"
                    
                    coord = config.REGION_COORDS.get(region, config.REGION_COORDS["서울"])
                    nx, ny = coord["nx"], coord["ny"]
                    
                    bd, bt = api_utils.get_kma_time()
                    _, w_res, _ = api_utils.forecast({"serviceKey": os.environ.get("SERVICE_KEY", ""), "base_date": bd, "base_time": bt, "nx": nx, "ny": ny})
                    
                    w_key = "Sunny"
                    if w_res and any(x in w_res for x in ["비", "강수"]): w_key = "Rain"
                    elif w_res and "눈" in w_res: w_key = "Snow"
                    elif w_res and any(x in w_res for x in ["흐림", "구름"]): w_key = "Cloudy"

                    now_kst = datetime.now(timezone(timedelta(hours=9)))
                    h = now_kst.hour
                    t_zone = "새벽" if 0 <= h < 6 else "오전" if 6 <= h < 12 else "오후" if 12 <= h < 18 else "밤"
                    ctx = f"Weather_{w_key}_{t_zone}"
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

            weight_val = 1 if is_like else -1
            u_email = body.get("email")
            db_utils.save_feedback(device_id, ctx, real_kind_code, weight_val, u_email)

            if not is_like:
                db_utils.state_table.update_item(
                    Key={'deviceId': device_id},
                    UpdateExpression="set last_spray_time = :t",
                    ExpressionAttributeValues={':t': "2000-01-01T00:00:00"}
                )
                
                mapping = db_utils.get_spray_mapping(device_id)
                valid_kinds = set(db_utils._kind_ids())
                active_sprays = [spray for spray, kind in mapping.items() if int(kind) in valid_kinds]
                
                candidates = [s for s in active_sprays if s != spray_no]
                if not candidates:
                    candidates = active_sprays if active_sprays else [1, 2, 3, 4]
                    
                new_spray = random.choice(candidates)
                new_kind = db_utils.get_kind_from_spray(device_id, new_spray) or new_spray
                
                db_utils.save_feedback(device_id, ctx, new_kind, 1, u_email)
                db_utils.manage_mailbox(device_id, new_spray)
                
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({
                        "message": f"싫어요 반영! {new_spray}번 칸 향기로 우회 학습",
                        "spray": new_spray,
                        "context": ctx
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
            
            translated_ctx = best_context
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
