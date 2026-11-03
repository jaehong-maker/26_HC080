# 디퓨저 시스템 설정값, 지역별 좌표, 향기 종류 매핑 등 전역 상수를 정의해둔 파일임

import os


COOLDOWN_MINUTES = 0.00
CONSUMPTION_PER_SEC = 0.5
MAX_CAPACITY = 100.0

OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY", "")

SMTP_SERVER = os.environ.get("SMTP_SERVER", "smtp.gmail.com")
SMTP_PORT = int(os.environ.get("SMTP_PORT", "587"))
SENDER_EMAIL = os.environ.get("SENDER_EMAIL", "20210928pm@gmail.com")
SENDER_PASSWORD = os.environ.get("SENDER_PASSWORD", "")


REGION_COORDS = {
    "서울": {"nx": "60", "ny": "127"},

    "수원": {"nx": "60", "ny": "121"},
    "성남": {"nx": "61", "ny": "120"},
    "안양": {"nx": "60", "ny": "125"},
    "고양": {"nx": "57", "ny": "128"},
    "용인": {"nx": "62", "ny": "121"},
    "부천": {"nx": "55", "ny": "125"},
    "안산": {"nx": "58", "ny": "121"},
    "남양주": {"nx": "64", "ny": "128"},
    "화성": {"nx": "57", "ny": "119"},
    "평택": {"nx": "62", "ny": "114"},
    "의정부": {"nx": "61", "ny": "129"},
    "파주": {"nx": "56", "ny": "131"},
    "김포": {"nx": "55", "ny": "128"},
    "광명": {"nx": "58", "ny": "125"},
    "경기광주": {"nx": "65", "ny": "123"},
    "이천": {"nx": "68", "ny": "121"},
    "양주": {"nx": "60", "ny": "121"},
    "구리": {"nx": "61", "ny": "127"},
    "포천": {"nx": "64", "ny": "134"},
    "양평": {"nx": "60", "ny": "121"},
    "가평": {"nx": "69", "ny": "133"},
    "시흥": {"nx": "56", "ny": "122"},

    "인천": {"nx": "55", "ny": "124"},
    "강원": {"nx": "73", "ny": "134"},
    "충북": {"nx": "69", "ny": "107"},
    "충남": {"nx": "67", "ny": "100"},
    "대전": {"nx": "67", "ny": "100"},
    "세종": {"nx": "67", "ny": "100"},
    "전북": {"nx": "63", "ny": "89"},
    "광주": {"nx": "58", "ny": "74"},
    "광주광역시": {"nx": "58", "ny": "74"},
    "전남": {"nx": "58", "ny": "74"},
    "대구": {"nx": "89", "ny": "90"},
    "경북": {"nx": "102", "ny": "94"},
    "울릉도": {"nx": "102", "ny": "115"},
    "경남": {"nx": "98", "ny": "76"},
    "부산": {"nx": "98", "ny": "76"},
    "울산": {"nx": "102", "ny": "84"},
    "제주": {"nx": "52", "ny": "38"},
}


KIND_IDS = (1, 2, 3, 4, 5, 6, 7)

SCENT_KINDS = {
    1: "시트러스",
    2: "센달우드",
    3: "패츄리",
    4: "페퍼민트",
    5: "라벤더",
    6: "바닐라",
    7: "물",
}


DEFAULT_SPRAY_TO_KIND = {
    1: 1,  # 1번 펌프: 시트러스
    2: 2,  # 2번 펌프: 센달우드
    3: 3,  # 3번 펌프: 패츄리
    4: 4,  # 4번 펌프: 페퍼민트
}


AMBIENT_DEFAULT_SCENT = 1
AMBIENT_DURATION_SECONDS = 60  # 서버가 명령 한 번으로 허용하는 분사 시간
AMBIENT_ANALYSIS_WINDOW_SECONDS = 60  # 테스트용 1분 분석 구간 (기존 180초)
AMBIENT_MUSIC_ROTATION_SECONDS = 60  # 종료 보고가 없는 구형 펌웨어의 음악 교체 주기
AMBIENT_INITIAL_DURATION_SECONDS = AMBIENT_DURATION_SECONDS
AMBIENT_RENEWAL_MARGIN_SECONDS = 30  # 만료 30초 전 다음 분사 명령을 발행
AMBIENT_STARTUP_MEASUREMENT_SECONDS = 3
AMBIENT_MIN_SCENT_PERCENT = 15  # 펌웨어의 잔량 차단 기준과 동일
AMBIENT_SAMPLE_GAP_RESET_SECONDS = 10
AMBIENT_MAX_SAMPLES = 600
AMBIENT_HIGH_THRESHOLD_DB = 60
AMBIENT_RECOVERY_THRESHOLD_DB = 60
AMBIENT_SPIKE_DELTA_DB = 15
AMBIENT_SCENT_IDS = (1, 2, 3, 4, 5, 6)
AMBIENT_DEVICE_SCENT_IDS = (1, 2, 3, 4, 5, 6)

AMBIENT_TIME_SLOTS = (
    {"label": "Dawn", "start": 0, "end": 6},
    {"label": "Morning", "start": 6, "end": 12},
    {"label": "Afternoon", "start": 12, "end": 18},
    {"label": "Night", "start": 18, "end": 24},
)

# 소음 구간: Quiet < 60, Moderate 60~79, Loud >= 80.
# max는 미포함 경계이므로 소수 평균값 79.9도 Moderate로 분류함.
AMBIENT_NOISE_BUCKETS = (
    {"label": "Quiet", "min": 0, "max": 60},
    {"label": "Moderate", "min": 60, "max": 80},
    {"label": "Loud", "min": 80, "max": None},
)

# 소음 구간별 고정 향기 우선순위. 7번 물은 앰비언트 추천에서 제외함.
AMBIENT_SCENTS_BY_BUCKET = {
    # 라벤더 -> 바닐라 -> 센달우드 -> 패츄리 -> 시트러스 -> 페퍼민트
    "Quiet": (5, 6, 2, 3, 1, 4),
    # 센달우드 -> 패츄리 -> 라벤더 -> 시트러스 -> 바닐라 -> 페퍼민트
    "Moderate": (2, 3, 5, 1, 6, 4),
    # 페퍼민트 -> 시트러스 -> 라벤더 -> 센달우드 -> 패츄리 -> 바닐라
    "Loud": (4, 1, 5, 2, 3, 6),
}

# 설정 오류로 구간 목록이 비어 있을 때만 사용할 안전 대체 순서임.
AMBIENT_SCENT_FALLBACK_ORDER = (5, 6, 2, 3, 4, 1)

# 앱의 실제 1~25번 트랙 목록을 곡의 분위기로 분류함.
# 번호 크기는 분위기와 무관하며 26번은 테스트 음원이므로 추천에서 제외함.
AMBIENT_MUSIC_CATALOG = {
    1: {"title": "여행", "artist": "볼빨간사춘기"},
    2: {"title": "Love Lee", "artist": "악동 뮤지션"},
    3: {"title": "바이,썸머", "artist": "아이유"},
    4: {"title": "Ai Wo Tsutaetaidatoka", "artist": "아이묭"},
    5: {"title": "사계 - 봄", "artist": "비발디"},
    6: {"title": "숲", "artist": "최유리"},
    7: {"title": "salty", "artist": "데이먼스이어"},
    8: {"title": "서울의 잠 못 이루는 밤", "artist": "10CM X 이수현"},
    9: {"title": "언제라도 어디에서라도", "artist": "정승환"},
    10: {"title": "사계 - 가을", "artist": "비발디"},
    11: {"title": "비", "artist": "폴킴"},
    12: {"title": "소나기", "artist": "이클립스"},
    13: {"title": "잠 못 드는 밤 비는 내리고", "artist": "아이유"},
    14: {"title": "비 오는날 듣기좋은 노래", "artist": "에픽하이"},
    15: {"title": "사계 - 여름", "artist": "비발디"},
    16: {"title": "첫 눈", "artist": "EXO"},
    17: {"title": "겨울잠", "artist": "아이유"},
    18: {"title": "첫 눈처럼 너에게 가겠다", "artist": "에일리"},
    19: {"title": "눈사람", "artist": "정승환"},
    20: {"title": "사계 - 겨울", "artist": "비발디"},
    21: {"title": "킬링벌스", "artist": "창모"},
    22: {"title": "밤바다", "artist": "최유리"},
    23: {"title": "round and round", "artist": "jisokury"},
    24: {"title": "lemon tree", "artist": "정예원"},
    25: {"title": "그때가좋았어", "artist": "oysters"},
}

AMBIENT_MUSIC_BY_BUCKET = {
    "Quiet": (6, 7, 8, 9, 11, 17, 18, 19, 22, 23, 24, 25),
    "Moderate": (3, 4, 10, 12, 13, 14, 16, 20),
    "Loud": (1, 2, 5, 15, 21),
}
