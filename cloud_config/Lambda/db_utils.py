# DynamoDB와의 통신(상태, 로그, 설정 읽기/쓰기)을 전담하는 유틸리티 파일임


# DynamoDB 등 데이터베이스와의 통신 및 쿼리를 담당하는 유틸리티 모듈


import boto3
import random
import logging
from decimal import Decimal
from boto3.dynamodb.conditions import Attr, Key
import uuid
from datetime import datetime, timezone, timedelta
import config

logger = logging.getLogger()

dynamodb = boto3.resource("dynamodb")
log_table = dynamodb.Table("DiffuserLog")
state_table = dynamodb.Table("DiffuserState")
pref_table = dynamodb.Table("DiffuserPref")
user_table = dynamodb.Table("DiffuserUsers")
auth_table = dynamodb.Table("DiffuserAuth")
ambient_table = dynamodb.Table("AmbientWeights")
diary_table = dynamodb.Table("UserDiaries")


def _get_kst_now():
    return datetime.now(timezone(timedelta(hours=9)))


# 사용자 감정 일기를 DB에 저장함
def save_diary(email, diary_text, emotion, scent_text):
    """
    사용자의 일기와 감정 분석 결과를 UserDiaries 테이블에 저장함.
    """
    try:
        now = _get_kst_now()
        item = {
            'email': email,
            'timestamp': now.isoformat(),
            'diary_id': str(uuid.uuid4()),
            'text': diary_text,
            'emotion': emotion,
            'scent': scent_text,
            'date': now.strftime("%Y-%m-%d")
        }
        diary_table.put_item(Item=item)
        return True
    except Exception as e:
        logger.error(f"Save Diary Error: {e}")
        return False


# 사용자의 이전 일기 목록을 조회함
def get_user_diaries(email, limit=20):
    """
    사용자의 일기 목록을 최신순으로 가져옵니다.
    """
    try:
        res = diary_table.query(
            KeyConditionExpression=Key('email').eq(email),
            ScanIndexForward=False,  # 최신순
            Limit=limit
        )
        return res.get('Items', [])
    except Exception as e:
        logger.error(f"Get Diaries Error: {e}")
        return []


def _kind_ids():
    return [int(kind_id) for kind_id in getattr(config, "KIND_IDS", (1, 2, 3, 4, 5, 6))]


def _default_spray_to_kind():
    default_mapping = getattr(config, "DEFAULT_SPRAY_TO_KIND", None)
    if isinstance(default_mapping, dict):
        try:
            return {int(spray): int(kind) for spray, kind in default_mapping.items()}
        except Exception:
            pass
    return {1: 1, 2: 2, 3: 3, 4: 4}


def _parse_int(value, default=0):
    try:
        return int(float(value))
    except Exception:
        return default


def _normalize_kind_slots(raw_slots):
    if isinstance(raw_slots, list):
        source_values = raw_slots[:4]
    elif isinstance(raw_slots, str) and raw_slots.strip():
        source_values = raw_slots.replace(",", "_").split("_")[:4]
    elif raw_slots not in (None, ""):
        source_values = [raw_slots]
    else:
        source_values = []

    valid_kinds = set(_kind_ids())
    slots = []
    for value in source_values:
        kind = _parse_int(value, 0)
        slots.append(kind if kind in valid_kinds else 0)

    while len(slots) < 4:
        slots.append(0)

    return slots[:4]


def _mapping_to_kind_slots(mapping):
    return [_parse_int(mapping.get(spray_no), 0) for spray_no in range(1, 5)]


def _parse_spray_mapping(mapping_value):
    if isinstance(mapping_value, dict):
        return {int(k): int(v) for k, v in mapping_value.items() if 1 <= int(k) <= 4}

    if isinstance(mapping_value, list):
        return {idx + 1: int(kind) for idx, kind in enumerate(_normalize_kind_slots(mapping_value))}

    if isinstance(mapping_value, str) and mapping_value.strip():
        mapping = {}
        for pair in mapping_value.split(","):
            if ":" in pair:
                spray, kind = pair.split(":", 1)
                spray_no = _parse_int(spray, 0)
                kind_no = _parse_int(kind, 0)
                if 1 <= spray_no <= 4:
                    mapping[spray_no] = kind_no
        if mapping:
            return mapping

        slots = _normalize_kind_slots(mapping_value)
        return {idx + 1: kind for idx, kind in enumerate(slots)}

    return {}


def _ambient_scent_ids():
    return [int(scent_id) for scent_id in getattr(config, "AMBIENT_SCENT_IDS", _kind_ids())]


def _ambient_device_scent_ids():
    configured = getattr(config, "AMBIENT_DEVICE_SCENT_IDS", None)
    if configured is not None:
        return [int(scent_id) for scent_id in configured]
    return _ambient_scent_ids()


def _build_ambient_seed_update():
    assignments = [
        f"scent_{scent_id}_score = if_not_exists(scent_{scent_id}_score, :def_weight)"
        for scent_id in _ambient_scent_ids()
    ]
    return "SET " + ", ".join(assignments)


def _read_ambient_scores(item):
    scores = {}
    for scent_id in _ambient_scent_ids():
        scores[scent_id] = int(item.get(f"scent_{scent_id}_score", 50))
    return scores


# list_ambient_context_keys 처리를 담당함
def list_ambient_context_keys():
    context_keys = []
    for time_slot in getattr(config, "AMBIENT_TIME_SLOTS", ()):
        for noise_bucket in getattr(config, "AMBIENT_NOISE_BUCKETS", ()):
            context_keys.append(f"{time_slot['label']}_{noise_bucket['label']}")
    return context_keys


# ensure_ambient_context 처리를 담당함
def ensure_ambient_context(context_key):
    normalized_key = str(context_key or "").strip()
    if not normalized_key:
        return False

    ambient_table.update_item(
        Key={"context_key": normalized_key},
        UpdateExpression=_build_ambient_seed_update(),
        ExpressionAttributeValues={":def_weight": 50},
    )
    return True


# seed_ambient_defaults 처리를 담당함
def seed_ambient_defaults():
    seeded = 0
    for context_key in list_ambient_context_keys():
        try:
            ensure_ambient_context(context_key)
            seeded += 1
        except Exception as e:
            logger.error(f"Ambient Seed Error ({context_key}): {e}")
    return seeded


# 룰렛 알고리즘으로 앰비언트 모드 향기를 추천함
def get_ambient_recommendation(context_key, device_id):
    """
    get_ambient_recommendation 함수: 1-9-90 룰렛 알고리즘 (Softmax/Ratio) 적용
    """
    try:
        ensure_ambient_context(context_key)
        resp = ambient_table.get_item(Key={"context_key": str(context_key)})
        item = resp.get("Item") or {}

        scores = _read_ambient_scores(item)
        
        installed_kinds = set(_active_kinds(device_id))
        if not installed_kinds:
            installed_kinds = set(_default_spray_to_kind().values())

        # 장착된 향기만 필터링하여 최소 가중치 1 보장
        valid_scores = {k: max(1, scores.get(k, 50)) for k in installed_kinds}
        kinds = list(valid_scores.keys())
        weights = list(valid_scores.values())
        
        if not kinds:
            return int(getattr(config, "AMBIENT_DEFAULT_SCENT", 1)), scores
            
        # 룰렛(비율 배분) 돌리기
        best_kind = random.choices(kinds, weights=weights, k=1)[0]

        return best_kind, scores
    except Exception as e:
        logger.error(f"Ambient Recommendation Error: {e}")
        return int(getattr(config, "AMBIENT_DEFAULT_SCENT", 1)), {
            kind_id: 50 for kind_id in _ambient_scent_ids()
        }


# 앰비언트 모드 피드백을 가중치에 반영함
def save_ambient_feedback(context_key, scent_code, weight_change):
    """
    save_ambient_feedback 함수: 앱의 좋아요/별로예요 피드백 반영, 하한선 1 유지
    """
    try:
        normalized_key = str(context_key or "").strip()
        scent_no = int(scent_code)
        if scent_no not in _ambient_scent_ids():
            return False

        ensure_ambient_context(normalized_key)
        
        resp = ambient_table.get_item(Key={"context_key": normalized_key})
        item = resp.get("Item") or {}
        
        score_col = f"scent_{scent_no}_score"
        current_score = int(item.get(score_col, 50))
        
        new_score = current_score + int(weight_change)
        new_score = max(1, min(100, new_score))
        
        ambient_table.update_item(
            Key={"context_key": normalized_key},
            UpdateExpression=f"SET {score_col} = :val",
            ExpressionAttributeValues={":val": new_score},
        )
        return True
    except Exception as e:
        logger.error(f"Ambient Feedback Save Error: {e}")
        return False


# get_spray_mapping 처리를 담당함
def get_spray_mapping(device_id, email=None):
    """Return {spray_no: kind_no}. Supports both spray_mapping and kind_slots."""
    try:
        if email:
            try:
                user_res = user_table.get_item(Key={"email": email})
                user_item = user_res.get("Item") or {}
                if user_item.get("spray_mapping"):
                    mapping = _parse_spray_mapping(user_item.get("spray_mapping"))
                    if mapping:
                        return {**_default_spray_to_kind(), **mapping}
            except Exception as e:
                logger.error(f"User Table Mapping Load Error: {e}")

        res = state_table.get_item(Key={"deviceId": device_id})
        item = res.get("Item") or {}

        if item.get("spray_mapping") is not None:
            mapping = _parse_spray_mapping(item.get("spray_mapping"))
            if mapping:
                return {**_default_spray_to_kind(), **mapping}

        if item.get("kind_slots") is not None:
            slots = _normalize_kind_slots(item.get("kind_slots"))
            return {idx + 1: kind for idx, kind in enumerate(slots)}

        if item.get("scent_slots") is not None:
            slots = _normalize_kind_slots(item.get("scent_slots"))
            return {idx + 1: kind for idx, kind in enumerate(slots)}
    except Exception as e:
        logger.error(f"Spray Mapping Load Error: {e}")

    return _default_spray_to_kind()


# get_kind_slots 처리를 담당함
def get_kind_slots(device_id):
    return _mapping_to_kind_slots(get_spray_mapping(device_id))


# save_spray_mapping 처리를 담당함
def save_spray_mapping(device_id, mapping, email=None):
    try:
        parsed_mapping = _parse_spray_mapping(mapping)
        if not parsed_mapping:
            parsed_mapping = _default_spray_to_kind()

        normalized_mapping = {spray: parsed_mapping.get(spray, 0) for spray in range(1, 5)}
        kind_slots = _mapping_to_kind_slots(normalized_mapping)
        mapping_str = ",".join(f"{spray}:{kind}" for spray, kind in normalized_mapping.items())

        state_table.update_item(
            Key={"deviceId": device_id},
            UpdateExpression="set spray_mapping = :m, kind_slots = :ks",
            ExpressionAttributeValues={":m": mapping_str, ":ks": kind_slots},
        )
        
        if email:
            try:
                user_table.update_item(
                    Key={"email": email},
                    UpdateExpression="set spray_mapping = :m",
                    ExpressionAttributeValues={":m": mapping_str},
                )
            except Exception as e:
                logger.error(f"User Table Mapping Save Error: {e}")

        return normalized_mapping
    except Exception as e:
        logger.error(f"Spray Mapping Save Error: {e}")
        return None


# get_spray_from_kind 처리를 담당함
def get_spray_from_kind(device_id, kind_code):
    try:
        target_kinds = [int(k) for k in str(kind_code) if k.isdigit()]
        
        mapping = get_spray_mapping(device_id)
        kind_to_spray = {int(k): int(s) for s, k in mapping.items()}
        
        valid_sprays = []
        for tk in target_kinds:
            if tk in kind_to_spray:
                valid_sprays.append(str(kind_to_spray[tk]))
        
        if valid_sprays:
            unique_sprays = sorted(list(set(valid_sprays)))
            return int("".join(unique_sprays))
            
    except Exception as e:
        logger.error(f"Kind To Spray Error: {e}")
    return None

_SCENT_SIMILARITY = {
    1: [4, 5, 2, 6, 3], # 시트러스 -> 페퍼민트, 라벤더, 센달우드...
    2: [3, 5, 6, 4, 1], # 센달우드 -> 패츄리, 라벤더, 바닐라...
    3: [2, 5, 4, 1, 6], # 패츄리 -> 센달우드, 라벤더, 페퍼민트...
    4: [1, 5, 2, 3, 6], # 페퍼민트 -> 시트러스, 라벤더, 센달우드...
    5: [6, 2, 3, 4, 1], # 라벤더 -> 바닐라, 센달우드, 패츄리...
    6: [5, 2, 1, 4, 3], # 바닐라 -> 라벤더, 센달우드, 시트러스...
    7: [1, 5, 4, 2, 6, 3] # 물 -> 가벼운 향부터
}

# 기기에 장착된 향기 중 가장 적절한 향기를 매핑해줌
def get_smart_spray_mapping(device_id, kind_code):
    try:
        target_kinds = [int(k) for k in str(kind_code) if k.isdigit()]
        if not target_kinds:
            return 1, " (알림: 향기 인식 오류로 기본 1번 향기로 대체됩니다.)"
            
        mapping = get_spray_mapping(device_id)
        kind_to_spray = {int(k): int(s) for s, k in mapping.items()}
        
        valid_sprays = []
        found_kinds = []
        for tk in target_kinds:
            if tk in kind_to_spray:
                valid_sprays.append(str(kind_to_spray[tk]))
                found_kinds.append(tk)
                
        if valid_sprays:
            unique_sprays = sorted(list(set(valid_sprays)))
            mapped_code = int("".join(unique_sprays))
            
            if len(found_kinds) < len(target_kinds):
                missing = set(target_kinds) - set(found_kinds)
                return mapped_code, f" (알림: {','.join(map(str, missing))}번 향기가 미장착되어 장착된 {','.join(map(str, found_kinds))}번 향기만 분사합니다.)"
            return mapped_code, ""
            
        primary_target = target_kinds[0]
        installed_kinds = list(kind_to_spray.keys())
        
        if not installed_kinds:
            return 1, " (알림: 기기에 장착된 향기 정보가 없어 기본 1번 펌프가 분사됩니다.)"
            
        similar_list = _SCENT_SIMILARITY.get(primary_target, [1, 2, 3, 4, 5, 6, 7])
        
        fallback_kind = None
        for sim_kind in similar_list:
            if sim_kind in installed_kinds:
                fallback_kind = sim_kind
                break
                
        if fallback_kind is None:
            fallback_kind = installed_kinds[0]
            
        mapped_pump = kind_to_spray[fallback_kind]
        return mapped_pump, f" (알림: 추천 향기가 기기에 없어 가장 유사한 {fallback_kind}번 향기로 대체 분사합니다.)"
        
    except Exception as e:
        logger.error(f"Smart Mapping Error: {e}")
        return 1, " (알림: 매핑 에러로 기본 1번 향기로 대체됩니다.)"


# get_kind_from_spray 처리를 담당함
def get_kind_from_spray(device_id, spray_no):
    try:
        return get_spray_mapping(device_id).get(int(spray_no))
    except Exception as e:
        logger.error(f"Spray To Kind Error: {e}")
        return None


def _active_kinds(device_id):
    mapping = get_spray_mapping(device_id)
    valid_kinds = set(_kind_ids())
    return [int(kind) for kind in mapping.values() if int(kind) in valid_kinds]


# get_music_track 처리를 담당함
def get_music_track(device_id, spray_or_kind_code, is_pump=False):
    """Return a music track for the resolved spray slot.

    The argument may be a spray number, a kind number, or a blend-like code.
    If a kind is loaded in a spray slot, that slot's saved music is used.
    """
    original_code = spray_or_kind_code
    try:
        code_str = str(spray_or_kind_code)
        if not is_pump and len(code_str) >= 2 and 1 <= int(code_str) <= 89:
            candidates = [int(digit) for digit in code_str if int(digit) in _kind_ids()]
            spray_or_kind_code = random.choice(candidates) if candidates else int(code_str[0])

        code = int(spray_or_kind_code)
        if is_pump:
            spray_no = int(str(code)[0])
        else:
            spray_no = get_spray_from_kind(device_id, code)
            if spray_no is None and 1 <= code <= 4:
                spray_no = code

        res = state_table.get_item(Key={"deviceId": device_id})
        if "Item" in res and "music_tracks" in res["Item"]:
            track_list = str(res["Item"]["music_tracks"]).split("_")
            if spray_no is not None and 1 <= int(spray_no) <= 4 and len(track_list) == 4:
                raw_track = track_list[int(spray_no) - 1]
                first_track = raw_track.split(",")[0].strip()
                return int(first_track)
    except Exception as e:
        logger.error(f"Music Load Error: {e}")

    defaults = {1: 1, 2: 6, 3: 11, 4: 16}
    try:
        code = int(str(original_code)[0])
        if is_pump:
            spray_no = code
        else:
            spray_no = get_spray_from_kind(device_id, code)
        target = int(spray_no) if spray_no is not None else code
        return defaults.get(target, target)
    except Exception:
        return 0


def _get_email_by_device_id(device_id):
    try:
        resp = user_table.scan(
            FilterExpression=Attr("deviceId").eq(str(device_id)),
            ProjectionExpression="email",
            Limit=1,
        )
        items = resp.get("Items") or []
        if items:
            return str(items[0].get("email", "")).strip()
    except Exception as e:
        logger.error(f"User Email Load Error: {e}")
    return None


# save_feedback 처리를 담당함
def save_feedback(device_id, context, kind_code, weight_change, email=None):
    try:
        kind_no = int(kind_code)
        if kind_no not in _kind_ids():
            return False

        score_col = f"scent_{kind_no}_score"
        
        target_email = email or _get_email_by_device_id(device_id)
        if not target_email:
            logger.error("Feedback Save Error: email not found")
            return False
            
        pref_table.update_item(
            Key={"email": str(target_email), "context": str(context)},
            UpdateExpression=f"ADD {score_col} :val",
            ExpressionAttributeValues={":val": int(weight_change)},
        )
        return True
    except Exception as e:
        print(f"[DEBUG] 저장 실패 원인: {str(e)}")
        logger.error(f"Feedback Save Error: {e}")
        return False


# get_personalized_scent 처리를 담당함
def get_personalized_scent(device_id, context, default_kind, email=None):
    try:
        target_email = email or _get_email_by_device_id(device_id)
        if not target_email:
            return default_kind, "기본 추천(이메일 없음)"

        resp = pref_table.get_item(Key={"email": str(target_email), "context": str(context)})
        item = resp.get("Item")
        if not item:
            return default_kind, "기본 추천"

        scores = {kind: int(item.get(f"scent_{kind}_score", 0)) for kind in _kind_ids()}
        active_kinds = _active_kinds(device_id)
        candidate_scores = {kind: scores.get(kind, 0) for kind in active_kinds}

        if not candidate_scores:
            return default_kind, "기본 추천"

        max_score = max(candidate_scores.values())
        if max_score > 0:
            best_kinds = [kind for kind, score in candidate_scores.items() if score == max_score]
            if int(default_kind) in best_kinds:
                return default_kind, "가중치 확인(유지)"
            return random.choice(best_kinds), "사용자 선호 가중치 적용"

        return default_kind, "기본 추천"
    except Exception as e:
        logger.error(f"Personalization Error: {e}")
        return default_kind, "에러(기본값)"


# get_user_history_text 처리를 담당함
def get_user_history_text(device_id, email=None):
    try:
        target_email = email or _get_email_by_device_id(device_id)
        if not target_email:
            return "사용자 취향 데이터 없음"
            
        resp = pref_table.query(
            KeyConditionExpression=Key('email').eq(str(target_email))
        )
        items = resp.get("Items", [])
        if not items:
            return "사용자 취향 데이터 없음"
            
        history_lines = []
        for item in items:
            context = item.get("context", "")
            scores = []
            for kind in _kind_ids():
                score = int(item.get(f"scent_{kind}_score", 0))
                if score > 0:
                    scores.append(f"{kind}번 향기 선호")
                elif score < 0:
                    scores.append(f"{kind}번 향기 비선호")
            if scores:
                history_lines.append(f"[{context} 상황]: " + ", ".join(scores))
                
        if not history_lines:
            return "특별한 선호도 기록 없음"
        return "\n".join(history_lines)
    except Exception as e:
        logger.error(f"User History Error: {e}")
        return "사용자 데이터 로딩 에러"


# 기기가 수행할 다음 명령(메일박스)을 저장하거나 읽어옴
def manage_mailbox(device_id, save_cmd=None, save_region=None, save_duration=None, is_peek=False):
    try:
        if save_cmd is not None:
            update_exp = "set pending_cmd = :c, current_spray = :c"
            exp_vals = {":c": int(save_cmd)}

            if save_region:
                update_exp += ", pending_region = :r, last_region = :r"
                exp_vals[":r"] = str(save_region)

            if save_duration is not None:
                update_exp += ", pending_duration = :d"
                exp_vals[":d"] = int(save_duration)

            state_table.update_item(
                Key={"deviceId": device_id},
                UpdateExpression=update_exp,
                ExpressionAttributeValues=exp_vals,
            )
            logger.info(f"[Mailbox_Write] ID:{device_id} Cmd:{save_cmd}")
            return int(save_cmd)

        res = state_table.get_item(Key={"deviceId": device_id})
        if "Item" in res:
            item = res["Item"]
            cmd = int(item.get("pending_cmd", -1))
            saved_region = item.get("pending_region", "") or item.get("last_region", "")
            saved_duration = int(item.get("pending_duration", 3))

            current_spray = int(item.get("current_spray", -1))

            if cmd != -1:
                if not is_peek:
                    state_table.update_item(
                        Key={"deviceId": device_id},
                        UpdateExpression="set pending_cmd = :z, pending_region = :e, pending_duration = :d",
                        ExpressionAttributeValues={":z": -1, ":e": "", ":d": 3},
                    )
                    logger.info(f"[Mailbox_Read] ID:{device_id} Executing Cmd:{cmd}")
                else:
                    logger.info(f"[Mailbox_Peek] ID:{device_id} Peeked Cmd:{cmd}")

                return cmd, saved_region, saved_duration

            if is_peek and current_spray > 0:
                return current_spray, saved_region, saved_duration

            if saved_region:
                return -1, saved_region, saved_duration

        return -1, get_saved_region(device_id), 3
    except Exception as e:
        logger.error(f"Mailbox Error: {e}")
        return -1, get_saved_region(device_id), 3


# remember_last_region 처리를 담당함
def remember_last_region(device_id, region):
    try:
        normalized = str(region or "").strip()
        if not normalized:
            return
        state_table.update_item(
            Key={"deviceId": device_id},
            UpdateExpression="set last_region = :r",
            ExpressionAttributeValues={":r": normalized},
        )
    except Exception as e:
        logger.error(f"Remember Region Error: {e}")


# get_saved_region 처리를 담당함
def get_saved_region(device_id):
    try:
        resp = user_table.scan(
            FilterExpression=Attr("deviceId").eq(str(device_id)),
            ProjectionExpression="#r",
            ExpressionAttributeNames={"#r": "region"},
        )
        items = resp.get("Items") or []
        if items:
            region = str(items[0].get("region", "") or "").strip()
            if region:
                return region
    except Exception as e:
        logger.error(f"User Region Load Error: {e}")

    try:
        res = state_table.get_item(Key={"deviceId": device_id})
        item = res.get("Item") or {}
        region = str(item.get("last_region", "") or "").strip()
        if region:
            return region
    except Exception as e:
        logger.error(f"State Region Load Error: {e}")

    try:
        resp = log_table.query(
            KeyConditionExpression=Key("deviceId").eq(str(device_id)),
            ScanIndexForward=False,
            Limit=20,
        )
        for item in resp.get("Items") or []:
            region = str(item.get("region", "") or "").strip()
            mode = str(item.get("mode", "") or "").lower()
            if region and region != "Emotion" and ("weather" in mode or region in getattr(config, "REGION_COORDS", {})):
                return region
    except Exception as e:
        logger.error(f"Recent Weather Region Load Error: {e}")

    return ""


# get_latest_voice_region 처리를 담당함
def get_latest_voice_region(device_id="SS"):
    try:
        lookup_device_id = str(device_id or "SS")
        resp = log_table.query(
                KeyConditionExpression=Key("deviceId").eq(lookup_device_id),
                ScanIndexForward=False,
                Limit=30,
        )
        for item in resp.get("Items") or []:
            region = str(item.get("region", "") or "").strip()
            if region and region != "Emotion" and region in getattr(config, "REGION_COORDS", {}):
                logger.info(f"[VoiceRegion] Latest {lookup_device_id} region: {region}")
                return region
    except Exception as e:
        logger.error(f"Voice Region Load Error: {e}")

    return ""


# get_device_state 처리를 담당함
def get_device_state(device_id):
    try:
        res = state_table.get_item(Key={"deviceId": device_id})
        if "Item" in res:
            return res["Item"]
        return {
            "deviceId": device_id,
            "last_spray_time": "2000-01-01T00:00:00",
            "current_capacity": Decimal(str(config.MAX_CAPACITY)),
            "last_spray_code": 0,
            "weight_g": Decimal("0"),
            "weights": [Decimal("0"), Decimal("0"), Decimal("0"), Decimal("0")],
            "kind_slots": _mapping_to_kind_slots(_default_spray_to_kind()),
        }
    except Exception as e:
        logger.error(f"Device State Load Error: {e}")
        return None


def _normalize_weight_list(weights_value):
    normalized = []

    if isinstance(weights_value, list):
        source_values = weights_value[:4]
    else:
        source_values = [weights_value]

    for value in source_values:
        try:
            normalized.append(float(value))
        except Exception:
            normalized.append(0.0)

    while len(normalized) < 4:
        normalized.append(0.0)

    return normalized[:4]


# update_device_state 처리를 담당함
def update_device_state(device_id, last_time, new_capacity, spray_code, weights_value, weights_raw_value=None):
    try:
        normalized_weights = _normalize_weight_list(weights_value)
        kind_slots = get_kind_slots(device_id)
        kind_code = get_kind_from_spray(device_id, spray_code) or 0

        update_exp = (
            "set last_spray_time = :t, current_capacity = :c, "
            "last_spray_code = :s, last_kind_code = :k, "
            "kind_slots = :ks"
        )
        exp_vals = {
            ":t": last_time,
            ":c": Decimal(str(new_capacity)),
            ":s": int(spray_code),
            ":k": int(kind_code),
            ":ks": kind_slots,
        }

        if weights_raw_value is not None:
            normalized_raw = _normalize_weight_list(weights_raw_value)
            update_exp += ", weights_raw = :wr"
            exp_vals[":wr"] = [Decimal(str(weight)) for weight in normalized_raw]

        state_table.update_item(
            Key={"deviceId": device_id},
            UpdateExpression=update_exp,
            ExpressionAttributeValues=exp_vals,
        )
    except Exception as e:
        logger.error(f"Update Device State Error: {e}")
