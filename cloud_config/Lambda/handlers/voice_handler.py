# 녹음된 음성 파일을 분석하여 디퓨저 제어 명령으로 변환하는 음성 인식 핸들러임

import os
import json
import base64
import uuid
import logging
import re
from datetime import datetime, timedelta, timezone

import config
import db_utils
import api_utils

logger = logging.getLogger()

_DIRECT_SCENT_TERMS = (
    "시트러스",
    "샌달우드",
    "센달우드",
    "패츌리",
    "패츄리",
    "페퍼민트",
    "라벤더",
    "바닐라",
    "무향",
)


def _has_explicit_scent_request(transcript):
    normalized = str(transcript or "").strip().lower()
    if any(term in normalized for term in _DIRECT_SCENT_TERMS):
        return True
    return bool(re.search(r"(?:향기\s*)?[1-7]\s*번(?:\s*향기)?", normalized))

# 음성 데이터를 처리하고 기기 명령을 반환함
def handle_voice(event, device_id):
    headers = event.get("headers") or {}
    content_type = api_utils._get_header(headers, "content-type")
    x_mime = api_utils._get_header(headers, "x-mime-type")
    
    try:
        bucket = os.environ.get("AUDIO_BUCKET", "")
        if not bucket: return {"statusCode": 500, "body": json.dumps({"message": "Server Error: AUDIO_BUCKET not set"})}

        raw_mime = str(x_mime if x_mime else content_type).lower()
        actual_mime = raw_mime.split(";")[0].strip()

        if actual_mime == "audio/mp4":
            actual_mime = "audio/m4a"
        
        audio_format = "wav"
        if "mp4" in actual_mime: audio_format = "mp4"
        elif "webm" in actual_mime: audio_format = "webm"
        elif "m4a" in actual_mime: audio_format = "m4a"
        elif "mpeg" in actual_mime: audio_format = "mp3"

        raw_body = event.get("body", "") or ""
        is_b64 = bool(event.get("isBase64Encoded", False))
        
        if is_b64:
            first_decoded = base64.b64decode(raw_body)
            try:
                wav_bytes = base64.b64decode(first_decoded)
            except:
                wav_bytes = first_decoded
        else:
            try:
                if isinstance(raw_body, str) and (len(raw_body) > 100):
                    if "," in raw_body:
                        raw_body = raw_body.split(",")[1]
                    wav_bytes = base64.b64decode(raw_body)
                else:
                    wav_bytes = raw_body.encode("latin1")
            except Exception as e:
                logger.warning(f"[DECODE_FALLBACK] {e}")
                wav_bytes = raw_body if isinstance(raw_body, bytes) else raw_body.encode("utf-8")
        
        logger.info(f"[AUDIO_SIZE] {len(wav_bytes)} bytes")
        
        key = f"voice/{device_id}/{datetime.now(timezone(timedelta(hours=9))).strftime('%Y%m%d_%H%M%S')}_{uuid.uuid4().hex[:8]}.{audio_format}"
        api_utils.s3.put_object(Bucket=bucket, Key=key, Body=wav_bytes, ContentType=actual_mime)
        
        audio_b64_str = base64.b64encode(wav_bytes).decode('utf-8')
        
        # --- API Quota Protection (Rate Limiter) ---
        now_kst = datetime.now(timezone(timedelta(hours=9)))
        state = db_utils.get_device_state(device_id) or {}
        last_voice_time_str = state.get('last_voice_request_time')
        if last_voice_time_str:
            try:
                last_voice_time = datetime.fromisoformat(last_voice_time_str)
                if (now_kst - last_voice_time).total_seconds() < 30:
                    logger.warning(f"Voice Request Rate Limited for {device_id}")
                    return {
                        "statusCode": 200,
                        "body": json.dumps({"spray": 0, "message": "음성 인식 쿨타임 중입니다.", "transcript": "SILENCE"}, ensure_ascii=False)
                    }
            except Exception as e:
                logger.error(f"Rate Limiter Time parse error: {e}")
        
        try:
            db_utils.state_table.update_item(
                Key={'deviceId': device_id},
                UpdateExpression="set last_voice_request_time = :t",
                ExpressionAttributeValues={':t': now_kst.isoformat()}
            )
        except Exception as e:
            logger.error(f"RateLimit DB Update error: {e}")
        # -------------------------------------------

        user_history_str = db_utils.get_user_history_text(
            device_id,
            contexts=["AI_VOICE"],
        )
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
            
        kind_code, duration, result_text, led_dict, transcript = api_utils.ask_gemini_audio_analysis(
            audio_b64_str, actual_mime, user_history_text=user_history_str, slot_info_str=active_kinds_str
        )

        normalized_transcript = (transcript or "").strip()
        logger.info(f"[TRANSCRIPT] {normalized_transcript}")

        if normalized_transcript in ["SILENCE", "FAIL", "ERROR", "TIMEOUT", "인식된 내용 없음", ""]:
            msg = result_text if result_text else "음성을 인식하지 못했습니다."
            if normalized_transcript == "TIMEOUT": msg = "분석 시간이 초과되었습니다."
            return {
                "statusCode": 200,
                "body": json.dumps({
                    "spray": 0,
                    "message": msg,
                    "transcript": normalized_transcript
                }, ensure_ascii=False)
            }
        
        if kind_code == 0:
            spray_code = 0
        else:
            if not _has_explicit_scent_request(normalized_transcript):
                personalized_kind, preference_message = db_utils.get_personalized_scent(
                    device_id,
                    "AI_VOICE",
                    kind_code,
                )
                if personalized_kind != kind_code:
                    logger.info(
                        f"[VOICE_PREFERENCE] {kind_code} -> {personalized_kind} "
                        f"({preference_message})"
                    )
                    kind_code = personalized_kind
            spray_code = db_utils.get_spray_from_kind(device_id, kind_code)
            if not spray_code:
                spray_code = 1

        state = db_utils.get_device_state(device_id) or {}
        last_time_str = state.get('last_spray_time', "2000-01-01T00:00:00")
        now = datetime.now(timezone(timedelta(hours=9)))
        if spray_code > 0:
            try:
                last_time = datetime.fromisoformat(last_time_str)
                if (now - last_time).total_seconds() < (config.COOLDOWN_MINUTES * 60):
                    return {
                        "statusCode": 200,
                        "body": json.dumps({"spray": 0, "message": "쿨타임 중이라 분사할 수 없습니다.", "mode": "CoolDown"}, ensure_ascii=False)
                    }
            except (TypeError, ValueError):
                pass

        if spray_code >= 0:
            target_cmd = spray_code if spray_code > 0 else 0
            command_duration = int(duration) if spray_code > 0 else 0
            active_mode = "voice" if spray_code > 0 else "ready"
            voice_led = led_dict if led_dict and led_dict.get("led_r", -1) >= 0 else None
            saved_cmd = db_utils.manage_mailbox(
                device_id,
                target_cmd,
                save_duration=command_duration,
                save_active_mode=active_mode,
                save_active_scent=target_cmd,
                save_led=voice_led,
            )
            if saved_cmd != target_cmd:
                raise RuntimeError("음성 명령을 기기 메일박스에 저장하지 못했습니다.")
            duration = command_duration
            
            if spray_code > 0:
                current_cap = float(state.get('current_capacity', config.MAX_CAPACITY))
                new_cap = round(max(0.0, current_cap - (duration * config.CONSUMPTION_PER_SEC)), 2)
                db_utils.update_device_state(device_id, now.isoformat(), new_cap, spray_code, [0.0, 0.0, 0.0, 0.0])

        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({
                "spray": int(spray_code),
                "message": f"음성 인식 성공: {result_text}",
                "transcript": normalized_transcript,
                "duration": duration,
                "context": "AI_VOICE",
                "active_mode": "voice" if spray_code > 0 else "ready"
            }, ensure_ascii=False)
        }

    except Exception as e:
        logger.error(f"[VOICE_FATAL] {e}")
        return {"statusCode": 200, "body": json.dumps({"spray": 0, "message": f"음성 처리 에러: {str(e)}"}, ensure_ascii=False)}
