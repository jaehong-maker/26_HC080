# 날씨 모드 분석 등 여러 핸들러에서 공통으로 가져다 쓰는 함수들을 모아둔 파일임


# 핸들러에서 공통으로 사용되는 모드별 로직(날씨/감정)을 정의한 공통 모듈

import os
import json
import base64
import uuid
import logging
import random
from datetime import datetime, timedelta, timezone
from decimal import Decimal

import config
import db_utils
import api_utils

logger = logging.getLogger()

# 날씨 정보를 바탕으로 향기 분사 로직을 결정함
def logic_weather_mode(weather, humidity):

    spray_code = 1
    try:
        if float(humidity) >= 50.0:
            weather = "흐림(고습도)"
    except: pass
    
    if any(x in weather for x in ["비", "강수"]): spray_code = 3
    elif "눈" in weather: spray_code = 4
    elif "흐림" in weather or "구름" in weather: spray_code = 2
    else: spray_code = 1
    return spray_code, weather, "Weather_Mode", 3

# logic_emotion_mode 처리를 담당함
def logic_emotion_mode(user_emotion_input):

    now = datetime.now(timezone(timedelta(hours=9)))
    is_daytime = (9 <= now.hour < 18)
    time_label = "Day" if is_daytime else "Night"
    spray_code = 1
    emotion_name = "Unknown"
    if user_emotion_input in ["1", "신남"]:
        emotion_name = "Happy"
        spray_code = 1 if is_daytime else 2
    elif user_emotion_input in ["2", "편안함"]:
        emotion_name = "Relaxed"
        spray_code = 4
    elif user_emotion_input in ["3", "화남"]:
        emotion_name = "Angry"
        spray_code = 2 if is_daytime else 3
    elif user_emotion_input in ["4", "슬픔"]:
        emotion_name = "Sad"
        spray_code = 3 if is_daytime else 4
    return spray_code, f"{emotion_name}/{time_label}", "Emotion_Mode", (3 if is_daytime else 2)

def _normalize_weights(body_dict):

    normalized = []
    weights_data = body_dict.get("weights")

    if isinstance(weights_data, list):
        source_values = weights_data[:4]
    elif weights_data not in (None, ""):
        source_values = [weights_data]
    else:
        source_values = []

    if not source_values:
        source_values = [body_dict.get(f"w{i}") for i in range(1, 5)]

    for value in source_values:
        try:
            normalized.append(round(float(value), 1))
        except Exception:
            normalized.append(0.0)

    while len(normalized) < 4:
        normalized.append(0.0)

    return normalized[:4]

def _to_decimal_list(values):

    return [Decimal(str(value)) for value in values]

def _get_kst_now():

    return datetime.now(timezone(timedelta(hours=9)))

def _parse_int(value, default=0):

    try:
        return int(float(value))
    except Exception:
        return default

def _get_ambient_time_slot(now_kst=None):

    now_kst = now_kst or _get_kst_now()
    hour = int(now_kst.hour)

    for time_slot in getattr(config, "AMBIENT_TIME_SLOTS", ()):
        start = int(time_slot.get("start", 0))
        end = int(time_slot.get("end", 24))
        if start <= hour < end:
            return time_slot.get("label", "Night")

    return "Night"

def _get_ambient_noise_bucket(db_level):
    normalized_db = max(0, _parse_int(db_level, 0))

    for noise_bucket in getattr(config, "AMBIENT_NOISE_BUCKETS", ()):
        min_value = int(noise_bucket.get("min", 0))
        max_value = noise_bucket.get("max")
        if max_value is None and normalized_db >= min_value:
            return noise_bucket.get("label", "Loud"), normalized_db
        if normalized_db >= min_value and normalized_db < int(max_value):
            return noise_bucket.get("label", "Quiet"), normalized_db

    return "Loud", normalized_db

def _build_ambient_context(db_level, now_kst=None):
    now_kst = now_kst or _get_kst_now()
    time_slot = _get_ambient_time_slot(now_kst)
    noise_bucket, normalized_db = _get_ambient_noise_bucket(db_level)
    return f"{time_slot}_{noise_bucket}", time_slot, noise_bucket, normalized_db

def _build_ambient_response(scent_code, duration, context_key, db_level, result_text, device_id=None, scores=None):
    music_code = 0
    if device_id and scent_code > 0:
        music_code = db_utils.get_music_track(device_id, scent_code, is_pump=True)
        
    payload = {
        "cmd": int(scent_code),
        "dur": int(duration),
        "music": int(music_code),
        "spray": int(scent_code),
        "duration": int(duration),
        "context": f"Ambient_{context_key}",
        "ambient_context": context_key,
        "db_level": int(db_level),
        "result_text": result_text,
        "message": result_text,
        "mode": "Ambient_Mode"
    }
    if scores is not None:
        payload["scores"] = scores
    return payload

def _get_ambient_scent_ids():
    return [int(scent_id) for scent_id in getattr(config, "AMBIENT_SCENT_IDS", (1, 2, 3, 4))]
