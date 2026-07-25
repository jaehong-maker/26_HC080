# 📝[2025년 한이음 드림업 공모전 GitHub README 템플릿]
- 이 파일은 2025년 한이음 드림업 프로젝트를 수행하는 학생들에게 README 작성의 가이드라인을 제공하기 위해 제작되었습니다.
- [Git & Github 기초사용법 알아보기](https://github.com/hanium-dreamup-challenge/git_guide/blob/main/README_git_guide.md)
---
## **💡README 작성방법**
- 프로젝트에서 사용되는 소스코드를 레포지토리에 업로드 한 후, 아래 가이드에 따라 README.md파일을 작성해주세요.
- 필수 작성 항목(5가지) : 프로젝트 개요, 팀원 소개, 시스템 구성도, 작품 소개영상, 핵심 소스코드 
- 프로젝트 저장소명 규칙 : `https://github.com/깃허브계정명/프로젝트 번호`
- 예시) 깃허브 계정이 hanium이고, 프로젝트 번호가 25_HC001일 경우 -> `https://github.com/hanium/25_HC001`
- 아래 항목 및 내용은 이해를 돕기위한 예시입니다. 참고만 하되 자유롭게 추가 및 작성해주시기 바랍니다.

---

## **💡1. 프로젝트 개요**

**1-1. 프로젝트 소개**
- 프로젝트 명 : I2S 음향 분석 및 날씨 데이터 연동형 AI 스마트 디퓨저
- 프로젝트 정의 : 디지털 I2S 마이크 기반 실내 소음 분석 엔진과 실시간 기상청 날씨 데이터를 클라우드 단에서 유기적으로 결합하여, 사용자 정황 맥락에 최적화된 하이브리드 향기 레시피를 자율 산출하고 독립 4채널로 능동 제어하는 차세대 지능형 웰니스 가전 플랫폼


  <img width="400" height="400" alt="image" src="https://github.com/user-attachments/assets/b25d0039-dfc8-4a6f-85c0-567a92e4039f" /></br>

**1-2. 개발 배경 및 필요성**
- 기존 상용 제품의 한계 : 기존 실내 방향 가전은 수동적인 제어와 단순 타이머 분사 방식에 고착되어 있어, 실내외 환경(소음, 날씨 등) 변화를 능동적으로 반영하지 못함
- 자체 간섭 및 노이즈 문제 : 기기 구동 진동으로 인한 센서 값 왜곡이나, 하나의 노즐을 공유하여 이전 향기가 잔존하는 교차 오염 문제가 고질적으로 발생함
- 진입 장벽 완화의 필요성 : 생소한 전용 앱 설치를 강제하는 기존 방식의 사용자 이탈 문제를 해소하고, 누구나 쉽게 접근 가능한 통합 제어 환경이 필요함

**1-3. 프로젝트 특장점**
- 스마트 소음 분석 (메인 테마) : I2S 마이크를 활용하여 실내 소음을 정밀 분석하고, 기기 자체의 스피커 출력 소음을 회피하는 '무음 구간 수음 기법'을 통해 순수한 실내 공간의 소음 데이터만 수집
- 하드웨어 한계의 소프트웨어적 극복 : 물리적 스파이크 노이즈(진동)를 지수이동평균(EMA) 필터로 상쇄하고, 교차 오염을 방지하는 4채널 독립 제어 및 잔향 소거 쿨타임 도입
- 무결성 클라우드 연동 : AI가 추천한 향기가 기기에 물리적으로 장착되어 있지 않을 경우, 가장 유사한 계열의 향으로 자동 대체하는 폴백(Fallback) 알고리즘 적용
- No-App 접근성 : 카카오톡 챗봇 플랫폼을 활용하여 별도 앱 설치 없이 채팅창 내에서 기기 연결 및 원격 제어 완결

**1-4. 주요 기능**
- 소음 반영 앰비언트 모드 : 수집된 데시벨 및 소음 패턴 데이터를 기반으로 공간의 정황을 인지하고, 그에 맞는 최적의 혼향 비율과 백그라운드 음악을 자율적으로 변경
- AI 및 기상 데이터 연동 조향 : 기상청 API의 정량적 기상 데이터와 Gemini 기반 사용자 감정(일기 텍스트) 분석을 융합하여 맞춤형 향기 레시피 산출
- 하드웨어 안전 및 잔량 제어 : 4축 고정밀 로드셀로 잔량을 실시간 계측하며, 잔량 15% 미만 낙하 시 초음파 분사 모듈의 과열 및 공회전 방지를 위해 릴레이 전원을 즉각 차단
- 무선 펌웨어 업데이트(OTA) 및 로컬 제어 : Wi-Fi 환경에서의 OTA 지원과 Nextion HMI 터치 디스플레이를 통한 직관적인 오프라인 구동 지원

**1-5. 기대 효과 및 활용 분야**
- 기대 효과 : 수동 조작의 번거로움 없이 공간의 맥락(소음, 날씨)을 스스로 인지하여 최적의 분위기를 연출하며, 향기 섞임 방지 및 기기 안전 제어로 상용 제품급 신뢰성 제공
- 활용 분야 : 일반 스마트 홈 웰니스 가전뿐만 아니라, 상황 변화와 소음 유입이 잦은 카페, 전시관, 공유 오피스 등 B2B 다중 이용 시설의 공간 맞춤형 앰비언트 시스템으로 확장 가능
  
**1-6. 기술 스택**
- 프론트엔드 / 모바일 앱 : React 18, HTML5 / CSS3, Vite
- 백엔드 (서버리스) : Python 3, AWS Lambda
- 데이터베이스 및 스토리지 : AWS DynamoDB (NoSQL), AWS S3
- AI / ML : Google Gemini API (자연어 문맥 파싱 및 감정 분석)
- 임베디드 (펌웨어) : C / C++ (ESP32 MCU), FreeRTOS
- H/W 제어 및 디자인 : Arduino IDE, EasyEDA, Nextion Edito
  
---

## **💡2. 팀원 소개**
| <img width="80" height="100" src="https://github.com/user-attachments/assets/ab73bb1c-c1d4-464d-8ad3-635b45d5a8ae" > | <img width="80" height="100" alt="image" src="https://github.com/user-attachments/assets/c7f66b7c-ab84-41fa-8fba-b49dba28b677" > | <img width="80" height="100" alt="image" src="https://github.com/user-attachments/assets/c33252c7-3bf6-43cf-beaa-a9e2d9bd090b" > | <img width="80" height="100" alt="image" src="https://github.com/user-attachments/assets/0d5909f0-fc73-4ab9-be09-4d48e3e71083" > | <img width="80" height="100" alt="image" src="https://github.com/user-attachments/assets/c7f66b7c-ab84-41fa-8fba-b49dba28b677" > |
|:---:|:---:|:---:|:---:|:---:|
| **류재홍** | **김지훈** | **신양섭** | **이준선** | **이헌영** |
| • 개발총괄 <br> • UI/UX 기획 | • 백엔드 <br> • 프론트엔드 | • API 개발 <br> • DB 서버 구축 |• 데이터 분석 <br> • 전처리 | • 프로젝트 멘토 <br> • 기술 자문 |



---
## **💡3. 시스템 구성도**
> **(참고)** S/W구성도, H/W구성도, 서비스 흐름도 등을 작성합니다. 시스템의 동작 과정 등을 추가할 수도 있습니다.
- **[ 하드웨어 및 센서 구성 ]**
  - MCU : ESP32-WROOM-32E (엣지 단말 보드 메인 통제 및 Wi-Fi 통신)
  - 데이터 수집 센서부 : INMP441 I2S 모듈 (실내 소음 맥락 수집), 4축 고정밀 로드셀 및 HX711 모듈 (질량 정밀 계측)
  - 기기 출력부 : 4채널 독립 분리형 릴레이 스위칭 모듈, 4채널 독립식 초음파 진동자 액추에이터, Nextion 4.3인치 HMI 디스플레이
  - 오디오 출력 : DFPlayer Mini MP3 (안내 음성 출력 및 음악 재생용)
<img width="500" height="500" alt="image" src="https://github.com/user-attachments/assets/28fc8453-d1a0-4184-8fd0-130d93d18545" />


- **[ 주요 서비스 흐름도 (소음 반영 모드 중심) ]**
  - 소음 수집 및 물리적 신호 간섭 회피 : 기기 자체 스피커에서 나오는 오디오 출력이 마이크로 유입되어 소음 수치가 왜곡되는 피드백 간섭을 회피하기 위해, 스피커 출력이 멈춘 무음 구간에만 마이크 버퍼를 활성화하여 순수 실내 소음 정밀 수집
  - 클라우드 연산 및 지능형 조향 알고리즘 : 기기에서 전송되는 소음 데시벨 데이터는 AWS Lambda로 파싱되며, 사전에 구축된 1-9-90 법칙 기반 추천 모델을 거쳐 현재 상황에 가장 적합한 혼향 비율을 자율적으로 산출
  - 기기 안전 제어 및 잔향 소거 : 조향 변경 명령 하달 시, 이전 향기와의 불쾌한 혼향을 방지하기 위해 15분간 분사를 강제 제한하는 잔향 소거 쿨타임 레이어 가동

---
## **💡4. 작품 소개영상**
> **참고**: 썸네일과 유튜브 영상을 등록하는 방법입니다.
```Python
아래와 같이 작성하면, 썸네일과 링크등록을 할 수 있습니다.
[![영상 제목](유튜브 썸네일 URL)](유튜브 영상 URL)

작성 예시 : 저는 다음과 같이 작성하니, 아래와 같이 링크가 연결된 썸네일 이미지가 등록되었네요! 
[![한이음 드림업 프로젝트 소개](https://github.com/user-attachments/assets/16435f88-e7d3-4e45-a128-3d32648d2d84)](https://youtu.be/YcD3Lbn2FRI?si=isERqIAT9Aqvdqwp)
```
[![한이음 드림업 프로젝트 소개](https://github.com/user-attachments/assets/16435f88-e7d3-4e45-a128-3d32648d2d84)](https://youtu.be/YcD3Lbn2FRI?si=isERqIAT9Aqvdqwp)


---
## **💡5. 핵심 소스코드**
- 소스코드 설명 : API를 활용해서 자동 배포를 생성하는 메서드입니다.

```Java
    private static void start_deployment(JsonObject jsonObject) {
        String user = jsonObject.get("user").getAsJsonObject().get("login").getAsString();
        Map<String, String> map = new HashMap<>();
        map.put("environment", "QA");
        map.put("deploy_user", user);
        Gson gson = new Gson();
        String payload = gson.toJson(map);

        try {
            GitHub gitHub = GitHubBuilder.fromEnvironment().build();
            GHRepository repository = gitHub.getRepository(
                    jsonObject.get("head").getAsJsonObject()
                            .get("repo").getAsJsonObject()
                            .get("full_name").getAsString());
            GHDeployment deployment =
                    new GHDeploymentBuilder(
                            repository,
                            jsonObject.get("head").getAsJsonObject().get("sha").getAsString()
                    ).description("Auto Deploy after merge").payload(payload).autoMerge(false).create();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
```
