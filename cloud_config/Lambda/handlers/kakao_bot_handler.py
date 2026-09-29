# 카카오톡 챗봇 웹훅을 통해 들어오는 디퓨저 제어 요청을 처리하는 핸들러임

import json
import logging
import config

logger = logging.getLogger()

# handle_kakao_bot 처리를 담당함
def handle_kakao_bot(body):
    """
    카카오톡 챗봇의 요청을 처리하고 카카오 Skill 규격에 맞는 JSON을 반환함.
    """
    try:
        user_request = body.get("userRequest", {})
        utterance = user_request.get("utterance", "").strip()
        
        logger.info(f"[KAKAO_BOT] 발화 내용: {utterance}")
        
        # 1. 향기 종류 가이드
        if "향기" in utterance or "종류" in utterance:
            scents = config.SCENT_KINDS
            scent_list_text = "🌿 스마트 디퓨저가 지원하는 향기 목록입니다:\n\n"
            
            for code, name in scents.items():
                scent_list_text += f"{code}번: {name}\n"
                
            scent_list_text += "\n원하시는 향기가 있으시면 앱의 '수동 분사' 탭에서 분사해 보세요!"
            
            return _build_simple_text_response(scent_list_text)
            
        # 2. 사용법 가이드
        elif "사용법" in utterance or "가이드" in utterance or "설명" in utterance:
            guide_text = (
                "📖 스마트 디퓨저 핵심 사용법 가이드\n\n"
                "수동 모드: 앱에서 원하는 향기와 지속 시간을 선택해 즉시 분사합니다.\n"
                "날씨 모드: 기상청 날씨 데이터를 분석하여 오늘 날씨에 딱 맞는 향기를 알아서 뿜어줍니다.\n"
                "음성 모드: 앱에 오늘 하루의 기분을 말하거나, 향기를 지시하면 맞는 향기를 분사합니다.\n"
                "일기장 모드: 앱에 오늘 하루의 기분을 남겨주시면, 위로가 되는 향기를 분사합니다.\n"
                "소음 모드: 디퓨저가 주변 소음과 시간을 감지해 방을 항상 향기롭게 유지합니다.\n\n"
                "더 자세한 설정은 앱의 설정 탭을 확인해 주세요!"
            )
            return _build_simple_text_response(guide_text)
            
        # 3. 초기 설정 (Wi-Fi 연결) 가이드
        elif "설정" in utterance or "초기" in utterance or "연결" in utterance or "와이파이" in utterance or "wifi" in utterance.lower():
            setup_text = (
                "⚙️ 스마트 디퓨저 초기 설정 안내\n\n"
                "① 기기 뒷면에 USB-C 전원을 연결해 켭니다.\n"
                "② 스마트폰 Wi-Fi 목록에서 'SmartDiffuser_Setup'을 찾아 연결하세요.\n"
                "③ 브라우저에 설정 페이지가 열리면, 집에서 사용하시는 Wi-Fi 이름과 비밀번호를 입력합니다.\n"
                "④ 기기가 재부팅되며 디스플레이에 날씨와 잔량이 표시되면 연결 성공입니다!"
            )
            return _build_simple_text_response(setup_text)

        # 4. 영점 (Tare) 설정 가이드
        elif "영점" in utterance or "무게" in utterance or "초기화" in utterance:
            tare_text = (
                "⚖️ 잔량 100% 영점 잡기 안내\n\n"
                "① 4개의 향기 용기를 모두 비운 상태로 기기 슬롯 정중앙에 올립니다.\n"
                "② 기기 디스플레이 메뉴에서 '설정' 버튼을 누릅니다.\n"
                "③ 잠시 기다리면 4개의 잔량이 모두 0%로 초기화됩니다.\n"
                "④ 이제 향수를 채우시면 잔량이 100%까지 정확하게 측정됩니다!"
            )
            return _build_simple_text_response(tare_text)

        # 5. 문제 해결 (Troubleshooting) 가이드
        elif "문제" in utterance or "고장" in utterance or "안돼" in utterance or "해결" in utterance:
            trouble_text = (
                "🛠️ 자주 묻는 질문 (문제 해결)\n\n"
                "Q. 설정용 Wi-Fi가 안 보여요.\n"
                "A. 디스플레이의 RE 버튼을 길게 눌러 초기화해 보세요.\n\n"
                "Q. 분사 버튼을 눌러도 작동 안 해요.\n"
                "A. 잔량이 15% 미만이면 모터 보호를 위해 분사가 차단됩니다. 향수를 보충해 주세요.\n\n"
                "Q. 화면이 자꾸 꺼져요.\n"
                "A. 일정 시간 조작이 없으면 자동으로 화면이 꺼지는 Sleep 모드입니다. 화면을 한 번 터치하면 켜집니다."
            )
            return _build_simple_text_response(trouble_text)
            
        # 기본 응답 (이해하지 못했을 때)
        else:
            fallback_text = (
                "질문을 이해하지 못했어요 😥\n"
                "아래와 같이 물어봐 주시면 답변해 드릴게요!\n\n"
                "👉 '향기 종류 알려줘'\n"
                "👉 '사용법 알려줘'\n"
                "👉 '초기 설정'\n"
                "👉 '문제 해결'"
            )
            return _build_simple_text_response(fallback_text)

    except Exception as e:
        logger.error(f"[KAKAO_BOT_ERROR] {e}")
        return _build_simple_text_response("서버에 일시적인 문제가 발생했습니다. 잠시 후 다시 시도해 주세요.")


def _build_simple_text_response(text):
    """
    카카오 i 오픈빌더의 SimpleText 규격으로 응답을 구성함.
    """
    response = {
        "version": "2.0",
        "template": {
            "outputs": [
                {
                    "simpleText": {
                        "text": text
                    }
                }
            ]
        }
    }
    
    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(response, ensure_ascii=False)
    }
