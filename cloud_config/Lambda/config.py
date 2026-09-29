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
AMBIENT_DURATION_SECONDS = 3
AMBIENT_SCENT_IDS = KIND_IDS
AMBIENT_DEVICE_SCENT_IDS = KIND_IDS

AMBIENT_TIME_SLOTS = (
    {"label": "Dawn", "start": 0, "end": 6},
    {"label": "Morning", "start": 6, "end": 12},
    {"label": "Afternoon", "start": 12, "end": 18},
    {"label": "Night", "start": 18, "end": 24},
)

AMBIENT_NOISE_BUCKETS = (
    {"label": "Quiet", "min": 0, "max": 40},
    {"label": "Moderate", "min": 40, "max": 70},
    {"label": "Loud", "min": 70, "max": None},
)
