# Gemini AI, 기상청 날씨 등 외부 API 연동과 관련된 통신 로직을 모아둔 파일임

import os
import json
import time
import uuid
import logging
import urllib.request
import urllib.parse
import urllib.error
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone
import smtplib
from email.mime.text import MIMEText
import boto3
import config

logger = logging.getLogger()

s3 = boto3.client("s3")
transcribe = boto3.client('transcribe')

# Gemini AI에게 감정 일기 분석 및 추천을 요청함
def ask_gemini_recommendation(diary_text, user_history_text, slot_info_str="1번 슬롯, 2번 슬롯, 3번 슬롯, 4번 슬롯"):
    """
    일기 내용과 사용자의 과거 취향을 함께 분석하여 향기를 추천함.
    """
    api_key = getattr(config, "GEMINI_API_KEY", os.environ.get("GEMINI_API_KEY", "")).strip()
    if not api_key:
        logger.error("Gemini API 키가 없습니다.")
        return "0", "AI 키 누락", {"led_r": -1, "led_g": -1, "led_b": -1, "led_bright": -1}, "감정 분석 실패"

    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={api_key}"

    prompt = f"""
    사용자의 오늘 일기: "{diary_text}"
    
    [사용자의 과거 향기 취향 데이터]
    {user_history_text}

    [현재 디퓨저에 장착된 향기 목록]
    {slot_info_str}
    
    이 일기를 분석하여 기분 전환에 가장 도움이 될만한 향기를 추천해줘.
    중요한 조건: 
    1. [절대 규칙] 과거의 선호도(취향 데이터)는 무시하셔도 좋습니다. "오늘 사용자가 작성한 일기의 감정(스트레스, 분노, 우울함 등)"을 해소하는 것이 100배 더 중요함.
    2. 과거에 '선호'했던 향기라도 오늘 감정 해소에 적합하지 않다면 **절대로** 추천하지 마세요. 감정 해소에 완벽한 향기를 최우선으로 고르세요.
    3. 단일 향기가 어울리면 1개만 고르고 시너지가 나면 2개를 블렌딩해라.
    4. 반드시 [현재 디퓨저에 장착된 향기 목록]에 명시된 향기 고유 번호 중에서만 골라주어야 한다. 장착되지 않은 향기는 절대 추천 금지.
    5. 추천할 향기의 고유 번호를 숫자로 대답해줘. (예: 1번 향기 1개면 1, 1번 향기와 3번 향기를 블렌딩하면 13, 2번과 4번이면 24). 그 숫자를 'kind' 필드에 넣어!! (슬롯 번호가 아니라 향기 고유 번호야!)
    6. 감정에 따라 조명 색상(led_r, led_g, led_b)도 매우 다양하게 추천해줘. 단, 기기(LED) 특성상 빛이 연하게(하얗게) 보일 수 있으므로 파스텔톤을 피하고 매우 진한 색상이 되도록 해줘! (RGB 중 가장 큰 값은 무조건 255에 가깝게, 작은 값은 0에 가깝게 원색 위주로 설정할 것)
    7. 오늘 하루 사용자의 감정을 요약한 짧은 문장을 'emotion_summary' 필드에 작성해줘.
    8. 사용자의 핵심 감정을 나타내는 단어(예: 피곤함, 우울함, 행복함, 짜증남)를 'emotion_tag' 필드에 작성해줘.
    9. 중요★ 'reason' 필드에 향기를 추천하는 이유를 적을 때 절대 번호(예: 1번 시트러스, 2번 라벤더)를 언급하지 마! 반드시 향기 이름(예: 시트러스 향과 라벤더 향)으로 자연스럽게 적어주세요.
    10. [매우 중요/금지어] 생성하는 모든 텍스트('reason', 'emotion_summary' 등) 내부에 "정지", "중지", "대기", "꺼짐", "멈춰", "스톱" 이라는 단어를 절대 사용하지 마세요. (기기 오작동을 유발할 수 있습니다. 대신 "해소", "완화", "휴식" 등의 단어로 대체하세요.)
    
    반드시 아래와 같은 순수 JSON 형태로만 대답하고 다른 설명글은 절대 쓰지 마: 
    (주의사항: 아래 예시의 값(15, 255, 100, 0)은 단지 예시일 뿐, 그대로 베끼지 말고, 매번 새롭게 분석하여 다채로운 값을 넣어줘!):
    {{"kind": 15, "led_r": 255, "led_g": 100, "led_b": 0, "led_bright": 200, "emotion_summary": "오늘 하루 감정은 짜증과 피곤함임.", "emotion_tag": "피곤함", "reason": "사용자의 일기 내용을 분석한 결과, 피로를 풀어줄 시트러스 향과 마음을 진정시킬 라벤더 향기를 추천함. 또한 편안한 오렌지빛 조명으로 설정하였습니다."}}
    """

    payload = json.dumps({
        "contents": [{"parts": [{"text": prompt}]}]
    }).encode('utf-8')

    max_retries = 3
    for attempt in range(max_retries):
        req = urllib.request.Request(url, data=payload, method='POST')
        req.add_header('Content-Type', 'application/json')

        try:
            with urllib.request.urlopen(req, timeout=15) as res:
                response_data = json.loads(res.read().decode('utf-8'))
                
                text_result = response_data['candidates'][0]['content']['parts'][0]['text']
                clean_text = text_result.replace("```json", "").replace("```", "").strip()
                result_json = json.loads(clean_text)
                
                kind_str = str(result_json.get("kind", "1"))
                reason = result_json.get("reason", "감정 분석 완료")
                emotion_summary = result_json.get("emotion_summary", "오늘의 감정을 분석했습니다.")
                emotion_tag = result_json.get("emotion_tag", "편안함")
                
                led_dict = {
                    "led_r": int(result_json.get("led_r", -1)),
                    "led_g": int(result_json.get("led_g", -1)),
                    "led_b": int(result_json.get("led_b", -1)),
                    "led_bright": int(result_json.get("led_bright", -1))
                }
                
                logger.info(f"[Gemini API 성공] 추천 코드(kind): {kind_str}, 이유: {reason}, 조명: {led_dict}, 감정요약: {emotion_summary}, 감정태그: {emotion_tag}")
                return kind_str, reason, led_dict, emotion_summary, emotion_tag

        except urllib.error.HTTPError as e:
            error_body = e.read().decode('utf-8')
            if e.code in [429, 500, 503, 504] and attempt < max_retries - 1:
                logger.warning(f"[Gemini API HTTP 에러] {e.code}. 재시도 {attempt+1}/{max_retries}")
                import time
                time.sleep(1.5)
                continue
            logger.error(f"[Gemini API HTTP 에러] {e.code} {e.reason} - 상세내용: {error_body}")
            return "0", f"AI 통신 에러: {e.code}", {"led_r": -1, "led_g": -1, "led_b": -1, "led_bright": -1}, "AI 통신 에러", "에러"
        except Exception as e:
            if attempt < max_retries - 1:
                logger.warning(f"[Gemini API 일반 에러] {e}. 재시도 {attempt+1}/{max_retries}")
                import time
                time.sleep(1.5)
                continue
            logger.error(f"[Gemini API 일반 에러] {e}")
            return "0", f"AI 분석 실패: {e}", {"led_r": -1, "led_g": -1, "led_b": -1, "led_bright": -1}, "AI 분석 실패", "에러"

# ask_gemini_ambient_mood 처리를 담당함
def ask_gemini_ambient_mood(db_avg, db_1hr_avg, is_spike, db_stddev):
    """
    소음 모드(Ambient Mode)에서 15분 쿨타임 만료 시 또는 Spike 발생 시 호출되어,
    현재 소음 환경에 어울리는 조명(LED)과 배경음악(Music)을 추천함.
    """
    api_key = getattr(config, "GEMINI_API_KEY", os.environ.get("GEMINI_API_KEY", "")).strip()
    if not api_key:
        logger.error("Gemini API 키가 없습니다.")
        return 0, {"led_r": -1, "led_g": -1, "led_b": -1, "led_bright": -1}

    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={api_key}"

    prompt = f"""
    현재 매장/공간의 소음 데이터를 분석하여 가장 어울리는 분위기의 조명과 음악 트랙 번호를 추천해줘.
    
    [소음 환경 데이터]
    - 현재 평균 소음: {db_avg}dB
    - 1시간 평균 소음: {db_1hr_avg}dB
    - 소음 변동성(표준편차): {db_stddev}
    - 급격한 소음 변화(Spike) 발생 여부: {is_spike}

    [조건]
    1. 'music' 필드에 1부터 25 사이의 숫자로 배경 음악 트랙 번호를 추천해줘. 
       - 소음이 적고 안정적이면 잔잔한 음악(낮은 번호대), 소음이 크거나 변화가 크면 활기찬 음악(높은 번호대) 추천.
    2. 'led_r', 'led_g', 'led_b' 필드에 0~255 사이의 RGB 조명 색상을 추천해줘.
       - 시끄러운 환경이면 차분한 색상(파란색, 보라색 계열) 또는 역동적인 색상(빨간색, 주황색).
       - 조용한 환경이면 따뜻한 색상(노란색, 초록색).
       - 조명은 파스텔톤을 피하고 가장 큰 값은 255에 가깝게 원색 위주로 설정해줘.
    3. 'led_bright' 필드에 0~255 사이의 밝기를 추천해줘. (기본적으로 100~255 사이)

    반드시 아래와 같은 순수 JSON 형태로만 대답해:
    {{"music": 6, "led_r": 255, "led_g": 180, "led_b": 0, "led_bright": 150}}
    """

    payload = json.dumps({
        "contents": [{"parts": [{"text": prompt}]}]
    }).encode('utf-8')

    max_retries = 2
    for attempt in range(max_retries):
        req = urllib.request.Request(url, data=payload, method='POST')
        req.add_header('Content-Type', 'application/json')

        try:
            with urllib.request.urlopen(req, timeout=10) as res:
                response_data = json.loads(res.read().decode('utf-8'))
                
                text_result = response_data['candidates'][0]['content']['parts'][0]['text']
                clean_text = text_result.replace("```json", "").replace("```", "").strip()
                result_json = json.loads(clean_text)
                
                music_code = int(result_json.get("music", 0))
                led_dict = {
                    "led_r": int(result_json.get("led_r", -1)),
                    "led_g": int(result_json.get("led_g", -1)),
                    "led_b": int(result_json.get("led_b", -1)),
                    "led_bright": int(result_json.get("led_bright", -1))
                }
                
                logger.info(f"[Gemini Ambient Mood] Music: {music_code}, LED: {led_dict}")
                return music_code, led_dict

        except Exception as e:
            logger.error(f"[Gemini Ambient API 에러] {e}")
            if attempt < max_retries - 1:
                import time
                time.sleep(1)
                continue
            return 0, {"led_r": -1, "led_g": -1, "led_b": -1, "led_bright": -1}


# get_kma_time 처리를 담당함
def get_kma_time():
    now = datetime.now(timezone(timedelta(hours=9)))
    if now.minute < 40: now -= timedelta(hours=1)
    return now.strftime("%Y%m%d"), now.strftime("%H00")

# forecast 처리를 담당함
def forecast(params):
    url = "http://apis.data.go.kr/1360000/VilageFcstInfoService_2.0/getUltraSrtNcst"
    try:
        service_key = params.pop('serviceKey')
        query_string = urllib.parse.urlencode(params)
        full_url = f"{url}?serviceKey={service_key}&{query_string}"
        
        req = urllib.request.Request(full_url)
        
        with urllib.request.urlopen(req, timeout=10) as response:
            if response.getcode() == 200:
                xml_str = response.read().decode('utf-8')
                
                if "NORMAL_SERVICE" not in xml_str:
                    logger.error(f"KMA API Error: {xml_str}")
                    return "0", "API_키_오류", "0"

                root = ET.fromstring(xml_str)
                t, p, h = "0", "0", "0"
                for item in root.findall(".//item"):
                    cat = item.findtext("category")
                    val = item.findtext("obsrValue")
                    if cat == "T1H": t = val
                    elif cat == "PTY": p = val
                    elif cat == "REH": h = val
                w = "강수" if p != "0" else "맑음"
                return t, w, h
    except Exception as e:
        logger.error(f"Forecast Error: {e}")
        return "0", "통신에러", "0"
    return "0", "에러", "0"

def _get_header(headers: dict, key: str) -> str:
    if not headers or not isinstance(headers, dict):
        return ""
    for k, v in headers.items():
        if str(k).lower() == key.lower():
            return v
    return ""

def _guess_device_id(event, body_dict):
    qs = event.get("queryStringParameters") or {}
    headers = event.get("headers") or {}
    device_id = ""

    if isinstance(body_dict, dict):
        device_id = body_dict.get("device", "") or body_dict.get("deviceId", "")

    if not device_id:
        device_id = qs.get("deviceId") or qs.get("device") or ""

    if not device_id:
        device_id = _get_header(headers, "x-device-id")

    return device_id if device_id else "ESP32_VOICE"

def _transcribe_audio_from_s3(bucket, key, language_code="ko-KR", media_format="wav"):
    job_name = f"diffuser-{int(time.time())}-{uuid.uuid4().hex[:8]}"
    media_uri = f"s3://{bucket}/{key}"

    try:
        transcribe.start_transcription_job(
            TranscriptionJobName=job_name,
            Media={"MediaFileUri": media_uri},
            MediaFormat=media_format,
            LanguageCode=language_code
        )

        for _ in range(12): 
            job = transcribe.get_transcription_job(TranscriptionJobName=job_name)
            status = job["TranscriptionJob"]["TranscriptionJobStatus"]
            
            if status == "COMPLETED":
                uri = job["TranscriptionJob"]["Transcript"]["TranscriptFileUri"]
                with urllib.request.urlopen(uri, timeout=10) as r:
                    txt = json.loads(r.read().decode('utf-8'))
                
                transcripts = txt.get("results", {}).get("transcripts", [])
                if transcripts:
                    result_text = (transcripts[0].get("transcript", "") or "").strip()
                    if result_text:
                        logger.info(f"[STT 성공] {result_text}")
                        return result_text
                    return "SILENCE"
                return "SILENCE"
                
            elif status == "FAILED":
                logger.error(f"[STT 실패] {job['TranscriptionJob'].get('FailureReason')}")
                return "FAIL"
            
            time.sleep(2) # 2초 대기
            
    except Exception as e:
        logger.error(f"[Transcribe Error] {e}")
        return "ERROR"
    
    return "TIMEOUT"

def _voice_to_spray(transcript: str):
    t = (transcript or "").strip().lower()
    spray_code = 0
    duration = 3
    result_text = f"VOICE: {transcript}"

    if any(k in t for k in ["맑", "상쾌", "시트러스", "레몬", "오렌지","대금","하여금","말금","낡은","밝은"]):
        spray_code = 1
        result_text = f"맑음(키워드): {transcript}"
    elif any(k in t for k in ["흐림", "구름", "차분", "라벤더", "릴렉스", "편안", "흐","그림", "짜증나", "기분좋아"]):
        spray_code = 2
        result_text = f"흐림(키워드): {transcript}"
    elif any(k in t for k in ["비", "강수", "레인", "우울", "진정", "바다", "아쿠아","피","삐","b","기","지","디","d" , "우울해"]):
        spray_code = 3
        result_text = f"비(키워드): {transcript}"
    elif any(k in t for k in ["눈", "스노우", "겨울", "민트", "쿨", "시원","누","문","론","돈", "슬프다"]):
        spray_code = 4
        result_text = f"눈(키워드): {transcript}"
    elif any(k in t for k in ["정지", "스톱", "멈춰", "꺼", "중지","멈처","멍처","먼처","멍춰","먼춰"]):
        spray_code = 0
        duration = 0
        result_text = f"정지(키워드): {transcript}"

    for sec in [1, 2, 3, 5, 10, 15]:
        if f"{sec}초" in t:
            duration = sec
            break

    return spray_code, duration, result_text, {"led_r": -1, "led_g": -1, "led_b": -1, "led_bright": -1}

# send_verification_email 처리를 담당함
def send_verification_email(receiver_email, code):
    try:
        msg = MIMEText(f"스마트 디퓨저 회원가입 인증번호입니다.\n\n인증번호: [{code}]\n\n앱으로 돌아가 인증번호를 입력해주세요.")
        msg['Subject'] = "[스마트 디퓨저] 회원가입 이메일 인증번호"
        msg['From'] = config.SENDER_EMAIL
        msg['To'] = receiver_email

        server = smtplib.SMTP(config.SMTP_SERVER, config.SMTP_PORT)
        server.starttls()
        server.login(config.SENDER_EMAIL, config.SENDER_PASSWORD)
        server.send_message(msg)
        server.quit()
        return True
    except Exception as e:
        logger.error(f"이메일 발송 실패: {e}")
        return False


# ask_gemini_audio_analysis 처리를 담당함
def ask_gemini_audio_analysis(audio_b64, mime_type, user_history_text="", slot_info_str=""):
    """
    사용자의 음성(오디오 데이터)을 Gemini 멀티모달로 직접 전달하여
    STT 변환(transcript)과 문맥 분석(kind, duration 등)을 동시에 수행함.
    과거 취향 데이터와 현재 장착된 향기 목록을 함께 분석함.
    """
    api_key = getattr(config, "GEMINI_API_KEY", os.environ.get("GEMINI_API_KEY", "")).strip()
    if not api_key:
        logger.error("Gemini API 키 누락")
        return 0, 3, "AI 키 누락", {}, "FAIL"

    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={api_key}"

    prompt = f"""
    [역할: 스마트 디퓨저 음성 비서]
    제공된 오디오 데이터를 정밀하게 분석하여 다음 규칙에 따라 JSON으로만 응답해줘.

    [참고용: 사용자의 과거 향기 취향 데이터]
    {user_history_text}
    (주의: 이 데이터는 단순 참고용임. 오늘 기분에 더 완벽히 맞는 향기가 있다면 철저히 무시하세요.)

    [현재 디퓨저에 장착된 향기 목록]
    {slot_info_str}

    1. **음성 인식 (STT)**: 사용자가 말한 내용을 정확히 'transcript'에 적어줘.
       - 만약 오디오에 사람의 목소리가 들리지 않거나, 배경 소음만 있다면 반드시 'transcript'를 "SILENCE"라고 적어줘. (이 경우 동작은 0)
    2. **향기 분석 (kind)**: 발화 내용(기분, 장소, 직접 요청 등)에 따라 가장 어울리는 향기 번호를 선택해.
       - [절대 규칙] 과거 취향 데이터보다 "사용자가 방금 음성으로 말한 기분이나 상태"를 해소하는 것이 100배 더 중요함. 과거 취향은 잊어버리셔도 좋습니다. 현재 감정에 가장 완벽한 향기를 고르세요.
       - 0: 정지 명령 (꺼, 멈춰 등)
       - 단일 향기가 어울리면 1개만 고르고 시너지가 나면 2개를 블렌딩해라.
       - 반드시 [현재 디퓨저에 장착된 향기 목록]에 명시된 향기 고유 번호 중에서만 골라주어야 한다. 장착되지 않은 향기는 절대 추천 금지.
       - 추천할 향기의 고유 번호를 숫자로 대답해줘. (예: 1번 향기 1개면 1, 1번 향기와 3번 향기를 블렌딩하면 13). 그 숫자를 'kind' 필드에 넣어!!
    3. **작동 시간 (duration)**: 언급된 시간이 있으면 초 단위로, 없으면 기본값 5를 넣어줘.
    4. **조명 추천**: 상황에 어울리는 LED 색상(led_r, g, b: 0~255)과 밝기(led_bright: 0~255)를 정해줘. 파스텔톤 말고 원색 위주로!

    [주의] 절대 다른 설명 없이 아래 JSON 형식만 출력해:
    {{"transcript": "발화 내용 또는 SILENCE", "kind": 번호, "duration": 초, "led_r": 0~255, "led_g": 0~255, "led_b": 0~255, "led_bright": 0~255, "reason": "추천 이유"}}
    """

    payload_dict = {
        "contents": [{
            "parts": [
                {"text": prompt},
                {
                    "inline_data": {
                        "mime_type": mime_type,
                        "data": audio_b64
                    }
                }
            ]
        }]
    }
    
    payload = json.dumps(payload_dict).encode('utf-8')
    req = urllib.request.Request(url, data=payload, method='POST')
    req.add_header('Content-Type', 'application/json')

    try:
        with urllib.request.urlopen(req, timeout=15) as res:
            response_data = json.loads(res.read().decode('utf-8'))
            text_result = response_data['candidates'][0]['content']['parts'][0]['text']
            
            clean_text = text_result.replace("```json", "").replace("```", "").strip()
            result_json = json.loads(clean_text)
            
            transcript = result_json.get("transcript", "인식된 내용 없음")
            kind_code = int(result_json.get("kind", 1))
            duration = int(result_json.get("duration", 3))
            reason = result_json.get("reason", "음성 분석 완료")
            
            led_dict = {
                "led_r": int(result_json.get("led_r", -1)),
                "led_g": int(result_json.get("led_g", -1)),
                "led_b": int(result_json.get("led_b", -1)),
                "led_bright": int(result_json.get("led_bright", -1))
            }
            
            logger.info(f"[Gemini Audio 성공] T:{transcript}, K:{kind_code}, R:{reason}")
            return kind_code, duration, f"AI분석: {reason}", led_dict, transcript
            
    except urllib.error.HTTPError as e:
        error_body = e.read().decode('utf-8')
        logger.error(f"[Gemini Audio HTTP 에러] {e.code}: {error_body}")
        return 0, 3, f"Gemini 에러({e.code}): {error_body}", {}, "ERROR"
    except Exception as e:
        logger.error(f"[Gemini Audio 예외 에러] {e}")
        return 0, 3, f"음성 분석 실패: {str(e)}", {}, "ERROR"

# ask_gemini_voice_analysis 처리를 담당함
def ask_gemini_voice_analysis(transcript, user_history_text="", slot_info_str=""):
    """
    STT로 변환된 사용자의 음성 텍스트를 Gemini가 분석하여
    문맥에 맞는 향기(spray_code)와 작동 시간(duration)을 반환함.
    """
    api_key = getattr(config, "GEMINI_API_KEY", os.environ.get("GEMINI_API_KEY", "")).strip()
    if not api_key:
        logger.error("Gemini API 키 누락")
        return _voice_to_spray(transcript) # 실패 시 기존 로직으로 Fallback

    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={api_key}"

    prompt = f"""
    [역할: 스마트 디퓨저 음성 비서]
    사용자의 스마트 디퓨저 음성 명령(STT 결과)을 정밀하게 분석하여 JSON으로만 응답해줘.
    
    명령 내용: "{transcript}"

    [참고용: 사용자의 과거 향기 취향 데이터]
    {user_history_text}
    (주의: 이 데이터는 단순 참고용임. 오늘 기분에 더 완벽히 맞는 향기가 있다면 철저히 무시하세요.)

    [현재 디퓨저에 장착된 향기 목록]
    {slot_info_str}

    [규칙]
    1. 사용자의 감정, 상황, 직접 요청 등을 파악하여 가장 어울리는 향기 번호를 'kind'에 넣을 것.
       - [절대 규칙] 과거 취향 데이터보다 "사용자가 방금 말한 기분이나 상태"를 해소하는 것이 100배 더 중요함. 과거 취향은 잊어버리셔도 좋습니다. 현재 감정에 가장 완벽한 향기를 고르세요.
       - 단일 향기가 어울리면 1개만 고르고 시너지가 나면 2개를 블렌딩해라.
       - 반드시 [현재 디퓨저에 장착된 향기 목록]에 명시된 향기 고유 번호 중에서만 골라주어야 한다. 장착되지 않은 향기는 절대 추천 금지.
       - 추천할 향기의 고유 번호를 숫자로 대답해줘. (예: 1번 향기 1개면 1, 1번 향기와 3번 향기를 블렌딩하면 13). 그 숫자를 'kind' 필드에 넣어!!
       - 만약 정지 명령(꺼, 멈춰 등)이거나 침묵이면 0을 넣을 것.
    2. 시간(초, 분)에 대한 직접적인 언급이 있으면 'duration'에 초 단위로 반영하고, 없으면 기본값 3을 넣을 것.
    3. 분위기에 어울리는 무드등 조명 색상(led_r, led_g, led_b)과 밝기(led_bright, 0~255)도 함께 추천할 것. 파스텔톤 말고 진한 색상으로!
    4. 다른 말은 절대 쓰지 말고 오직 순수 JSON만 반환할 것.

    예시 응답:
    {{"kind": 13, "duration": 5, "led_r": 0, "led_g": 0, "led_b": 255, "led_bright": 180, "reason": "오늘 회사에서 짜증이 났다고 하여 1번 시트러스와 3번 패츄리를 섞어 차분한 분위기를 연출함"}}
    """

    payload = json.dumps({
        "contents": [{"parts": [{"text": prompt}]}]
    }).encode('utf-8')

    req = urllib.request.Request(url, data=payload, method='POST')
    req.add_header('Content-Type', 'application/json')

    try:
        with urllib.request.urlopen(req, timeout=5) as res:
            response_data = json.loads(res.read().decode('utf-8'))
            
            text_result = response_data['candidates'][0]['content']['parts'][0]['text']
            clean_text = text_result.replace("```json", "").replace("```", "").strip()
            result_json = json.loads(clean_text)
            
            kind_code = int(result_json.get("kind", 1))
            duration = int(result_json.get("duration", 3))
            reason = result_json.get("reason", "문맥 분석 완료")
            
            led_dict = {
                "led_r": int(result_json.get("led_r", -1)),
                "led_g": int(result_json.get("led_g", -1)),
                "led_b": int(result_json.get("led_b", -1)),
                "led_bright": int(result_json.get("led_bright", -1))
            }
            
            logger.info(f"[Gemini Voice 성공] 향기: {kind_code}, 조명: {led_dict}, 이유: {reason}")
            return kind_code, duration, f"AI분석: {reason}", led_dict
            
    except Exception as e:
        logger.error(f"[Gemini Voice 에러, 기존 로직으로 전환] {e}")
        return _voice_to_spray(transcript)
