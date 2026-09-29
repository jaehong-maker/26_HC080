# API Gateway 요청을 최초로 받아 각 기능별 핸들러로 라우팅해주는 메인 진입점 파일임

import sys
import os
import json
import logging

sys.path.append(os.path.dirname(__file__))

from handlers.common import _normalize_weights
import api_utils

from handlers.voice_handler import handle_voice
from handlers.auth_handler import handle_auth
from handlers.settings_handler import handle_settings
from handlers.device_handler import handle_device
from handlers.feedback_handler import handle_feedback
from handlers.ambient_handler import handle_ambient
from handlers.core_mode_handler import handle_core_modes
from handlers import kakao_bot_handler

logger = logging.getLogger()
logger.setLevel(logging.INFO)

# 모든 API 요청을 분류하고 적절한 핸들러로 넘겨줌
def lambda_handler(event, context):
    response = _route_request(event, context)
    
    # 처리된 명령에 대해 어떤 데이터를 반환하는지 로그 기록
    try:
        raw_body = event.get("body", "")
        parsed_preview = json.loads(raw_body) if isinstance(raw_body, str) and raw_body else raw_body
        if isinstance(parsed_preview, dict):
            action = parsed_preview.get("action", "")
            if action != "POLL" and response and "body" in response:
                resp_body = json.loads(response["body"])
                logger.info(f"📤 [명령 처리 완료] 클라이언트로 반환하는 데이터:\n{json.dumps(resp_body, indent=2, ensure_ascii=False)}")
    except Exception as e:
        pass
        
    return response

def _route_request(event, context):
    raw_body = event.get("body", "")
    parsed_preview = {}
    try:
        parsed_preview = json.loads(raw_body) if raw_body else {}
        action = parsed_preview.get("action", "")
        
        # POLL 요청인 경우 간단하게 핵심 데이터만 로그로 남김
        if action == "POLL":
            weights = parsed_preview.get("weights", [])
            db_level = parsed_preview.get("db_level", 0)
            is_app = "email" in parsed_preview
            sender = "모바일 앱" if is_app else "디퓨저 기기"
            
            if not is_app:
                logger.info(f"📡 [기기 POLL] {sender} 상태 업데이트 -> 무게: {weights}, 소음: {db_level}dB")
        else:
            logger.info("============== [REQUEST START] ==============")
            mode = parsed_preview.get("mode", "")
            action_desc = action
            if action == "AI_WEATHER": action_desc = f"날씨 모드 ({mode})"
            elif action == "MANUAL": action_desc = "수동 제어 모드"
            elif action == "DISPLAY_MODE": action_desc = "기기 디스플레이 화면 조작"
            elif action == "START": action_desc = "단순 분사 제어"
            elif action == "STOP_ALL" or action == "MENU_STOP": action_desc = "기기 정지 명령"
            
            logger.info(f"🚀 [새 명령 수신] {action_desc} 요청이 들어왔습니다.")
            logger.info(f"📩 [상세 데이터]\n{json.dumps(parsed_preview, indent=2, ensure_ascii=False)}")
            
    except Exception as e:
        logger.warning(f"⚠️ [BODY_PARSE_FAIL] {e}")

    device_id = api_utils._guess_device_id(event, parsed_preview)
    headers = event.get("headers") or {}
    content_type = api_utils._get_header(headers, "content-type")
    x_mime = api_utils._get_header(headers, "x-mime-type")
    is_audio = any(x in str(content_type).lower() for x in ["audio/", "video/webm"]) or \
               any(x in str(x_mime).lower() for x in ["audio/", "video/webm"])

    if is_audio:
        return handle_voice(event, device_id)

    raw_body = event.get("body", {})

    try:
        if isinstance(raw_body, str):
            body = json.loads(raw_body) if raw_body else {}
        elif isinstance(raw_body, dict):
            body = raw_body
        else:
            body = {}
    except Exception as e:
        logger.warning(f"[JSON_PARSE_ERROR] {e}")
        body = {}

    if "userRequest" in body:
        return kakao_bot_handler.handle_kakao_bot(body)

    device_id = api_utils._guess_device_id(event, body)
    normalized_weights = _normalize_weights(body)

    mode = body.get("mode", "weather")
    region = body.get("region") or body.get("data", "서울")
    action = body.get("action", "")
    if not (body.get("region") or body.get("data")):
        region = ""

    auth_response = handle_auth(action, body, device_id)
    if auth_response:
        return auth_response

    settings_response = handle_settings(action, body, device_id)
    if settings_response:
        return settings_response

    device_response = handle_device(action, mode, body, device_id, normalized_weights)
    if device_response:
        return device_response

    feedback_response = handle_feedback(action, mode, body, device_id)
    if feedback_response:
        return feedback_response

    ambient_response = handle_ambient(mode, body, device_id, normalized_weights)
    if ambient_response:
        return ambient_response

    if action == "AI_WEATHER":
        mode = "weather"
        logger.info(f"[AI_WEATHER_TRIGGER] Region: {region}")

    return handle_core_modes(action, mode, body, device_id, normalized_weights, region)
