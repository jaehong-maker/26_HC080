# 소음 분석 모드 개발자 전달사항

음악 종료 처리에 대한 서버 수정은 로컬에 반영했습니다. 아래 `music_finished` 송신은 하드웨어 담당자에게 요청할 규약이며, 이번 작업에서 펌웨어는 변경하지 않았습니다. 상세 전달문은 [소음모드 음악 연속 재생 전달사항](../docs/ambient-music-hardware-handoff.md)을 참고하세요. 실제 Lambda 배포는 아직 하지 않았습니다.

## 최초 3초 측정 후 연속 분사 정책 (분석 60초)

- 최초 ambient 진입 시 이전 출력을 멈추는 `spray=0` 명령을 POLL로 전달하고, `ambient_startup_pending=true`로 초기 측정을 시작합니다.
- 정지 명령을 전달하는 POLL의 소음값은 제외하고, 이후 새 POLL 측정값을 최소 3초 동안 모아 평균을 냅니다. 저장된 과거 평균이나 기본값으로 초기 향을 고르지 않습니다.
- POLL 간격이 약 2초이므로 유효 샘플이 걸치는 시간은 보통 약 4초입니다. 서버는 그 다음 응답에서 향을 확정하며, AI 처리·통신 시간도 추가됩니다. 정확히 3초간 연속 PCM을 수집하는 펌웨어 기능은 아닙니다.
- 초기 측정과 이후 이동평균은 `microphone_valid=true`와 유효한 `db_level`이 함께 있는 샘플만 사용합니다. 정상 `db_level=0`은 포함하며, false·필드 누락·비정상 수치는 제외합니다. 무효값을 0으로 대체하지 않습니다.
- 서버가 새 측정 평균에 맞는 장착 향을 선택하고 `duration=60` 명령을 한 번 저장합니다.
- 클라이언트가 `duration=3`을 보내도 앰비언트 명령에는 서버의 `AMBIENT_DURATION_SECONDS=60`을 적용합니다. 분석 시간 설정과 분사 허용 시간은 별도입니다.
- 시작 요청 때 기존 소음 샘플·자동 정지 타이머를 초기화하고, 초기 60초 분사 명령 발행 시 이후 이동평균 측정 구간도 새로 시작합니다.
- 음악·LED·분사 명령은 기기의 후속 POLL로 전달합니다. 명령 소비 후에는 `spray=-1`, `app_command=false`를 반환합니다.
- 최초 측정 중 중복 시작 요청은 진행 중인 분사 시간을 갱신하지 않습니다.
- 이후 유효한 최근 60초 평균이 Quiet(<60), Moderate(60~79), Loud(>=80)의 다른 구간으로 바뀌면 서버가 향·음악을 다시 선택하고 60초 분사 명령을 발행합니다. 구간 변경에서는 외부 AI를 기다리지 않고 새 구간 목록의 곡을 즉시 선택합니다. 조명은 기존 값을 유지하며, 최초 측정 완료 시의 AI 추천은 유지합니다.
- 무효 POLL은 마지막 유효 측정 시각을 갱신하지 않습니다. 마지막 유효 측정 이후 10초 이상 지나면 기존 샘플을 비워 측정을 재시작합니다. 초기 측정 중이면 초기 3초 측정도 다시 시작합니다. 무효 POLL은 연속 소음 유지 시간을 즉시 끊으며, 새 추천은 실행하지 않습니다.
- 마이크가 무효여도 잔량 갱신·분사 허용 갱신·대기 명령·사용자 정지는 계속 처리합니다. 응답은 `microphone_valid=false`와 함께 `db_level`을 생략하고, 유효 측정 시각은 `db_last_sample_time`으로 확인할 수 있습니다.
- 같은 구간·같은 향이어도 연속 분사를 유지합니다. 기기에 명령을 전달한 시점부터 만료 시간을 계산하고, 만료 30초 전 다음 60초 명령을 한 번 발행합니다. 일반 POLL마다 반복 발행하지 않습니다.
- 펌웨어 수정 요청: 같은 구간에서 곡을 끝까지 재생하고, DFPlayer BUSY가 실제 재생(LOW)을 거쳐 500ms 이상 HIGH로 유지되면 현재 `music` 번호와 함께 `music_finished=true`를 POLL로 보고해 주세요. 로딩·짧은 신호 변화·명시적 정지는 곡 종료로 처리하지 않습니다. 보고는 다음 곡 명령을 받을 때까지 유지합니다.
- 서버는 종료 보고의 곡 번호가 현재 선택과 일치할 때 구간 목록의 다음 곡을 선택합니다. `music_finished=false`이면 60초가 지나도 곡을 바꾸지 않습니다. 이 필드가 없는 구형 펌웨어는 마지막 곡 선택 후 60초가 지나면 같은 구간의 다음 곡을 선택합니다. 음악 타이머는 마이크 유효성이나 새 60초 분석 구간을 기다리지 않으며, 현재 적용 중인 `ambient_context`의 목록을 사용합니다.
- 종료에 따른 음악 교체는 마이크의 새 분석 완료를 기다리지 않습니다. 음악만 교체할 때 향·LED·분사 일정은 유지하며, 기존 분사 갱신과 같은 POLL에 겹칠 수 있습니다. 분사 갱신이 없다면 `spray=-1`, `app_command=false`, `music=새 트랙 번호`입니다. 음악은 분사 명령 유무와 별개로 적용해 주세요. 음악 교체에는 AI를 호출하지 않습니다.
- 최초 AI 추천 결과는 구간별 곡 목록으로 검증합니다. 구간 변경은 AI 호출 없이 같은 검증 함수를 통해 새 구간의 곡을 선택합니다. Moderate는 3·4·10·12·13·14·16·20번만 사용합니다. 목록 밖의 곡(예: 6번 '숲')이나 AI 실패는 목록 안의 다른 곡으로 대체합니다. 번호가 낮으면 잔잔하다는 기존 AI 지시는 제거했습니다.
- 갱신에는 AI를 다시 호출하지 않습니다. 새 명령이 없는 POLL은 `spray=-1`, `app_command=false`이고, 동일한 음악은 `music=0`으로 유지합니다.
- 앱 POLL은 허용 시간을 갱신하거나 기기 명령을 소비하지 않습니다. 통신이 끊기면 기기는 마지막으로 허용받은 시간까지만 분사합니다.
- 정지·모드 변경 시 갱신을 해제하고, 이전 세션·명령 토큰에 대한 조건부 쓰기로 오래된 갱신이 되살아나지 않도록 합니다. 좋아요가 아닌 대체 향 분사에도 동일한 60초 허용 시간을 적용합니다.
- 기기 출력은 후속 POLL로 적용하며 직접 추천 응답은 진단용이라는 펌웨어 규약을 유지합니다. 별도 분사 중단 수정으로 DFPlayer의 ACK 대기를 해제했습니다. 센서·잔량 차단과 GPIO 제어 방식은 유지합니다.
- GitHub 업로드와 실제 Lambda 배포·실물 검증은 구분해야 합니다.

음악 진단용 POLL 필드: `ambient_music_policy="finished-or-60s-v2"`, `ambient_music_changed_at`, `ambient_music_rotation_seconds`, `ambient_renewal_enabled`, `device_reported_music`, `device_last_poll_time`, `device_music_finish_reporting`. 앱 POLL의 `music`은 서버가 선택한 번호이고, `device_reported_music`은 마지막 기기 POLL이 보고한 번호입니다. 이 값으로 실제 소리가 재생 중인지까지 확인할 수는 없습니다.

구간 변경 추가 수정본은 `Lambda_ambient_bucket_fix.zip`이며 `ambient_bucket_music_policy="immediate-bucket-v1"`로 구분합니다. 평균 구간 후보만 바뀌고 명령 저장에 실패했을 때는 `ambient_effective_db`를 먼저 바꾸지 않습니다. 실제 적용 중인 `ambient_context`와 함께 갱신합니다.

## 앱 개발자용 전달문

소음 분석 모드 서버 수정이 완료되었습니다. 앱에서는 아래 사항을 반영해 주세요.

### 1. 서버 모드 동기화

- 서버에서 `active_mode: "ambient"`를 받으면 앱의 `activeMode`를 `"noise"`로 설정해 주세요.
- 이때 디퓨저 전원 상태도 ON으로 표시해 주세요.
- 앱 재실행이나 화면 새로고침 후에도 `POLL` 응답을 기준으로 소음 모드를 복원해야 합니다.

### 2. `spray` 값 처리

서버 응답의 `spray` 값은 다음 의미를 가집니다.

- `spray > 0`: 새로운 향기 분사 명령
- `spray == -1`: 새 명령 없음. 현재 모드와 향기 유지
- `spray == 0`: 명시적인 정지

`spray == -1`은 정지나 오류로 처리하지 말고 기존 상태를 유지해 주세요.

### 3. 소음 모드 피드백

소음 모드 시작 응답 또는 `POLL` 응답에서 받은 `ambient_context`를 앱 상태에 저장해 주세요.

좋아요·싫어요 요청에는 다음 값을 포함해야 합니다.

```json
{
  "mode": "ambient",
  "ambient_context": "Morning_Quiet"
}
```

현재 서버는 기존 앱의 `NOISE_ANALYSIS` 형식도 호환하지만, 신규 앱에서는 실제 `ambient_context`를 전달해 주세요.

### 4. 소음 분석 화면

- 현재의 고정 15초 카운트다운은 제거해 주세요. 서버의 분석 구간은 테스트용 60초(기존 180초)입니다.
- 가능하면 다음 서버 필드로 실제 분석 상태를 표시해 주세요.
  - `db_window_ready`
  - `ambient_sustained_loud`
  - `ambient_context`
  - `ambient_effective_db`
- 측정값이 없거나 유효하지 않을 때 임의의 `45dB`, `42dB`를 표시하지 말고 `--` 또는 `측정 대기`로 표시해 주세요.
- 스파이크 발생 시 `급격한 소음 변화 감지` 정도로 표시해 주세요.
- 순간 스파이크만으로 향기가 즉시 변경되는 것은 아니므로 `분위기 전환 중` 문구는 제거해 주세요.

### 5. API 응답 타입

다음 필드를 앱 API 타입과 `DeviceContext`에 반영해 주세요.

```ts
ambient_context?: string;
ambient_effective_db?: number;
db_stddev?: number;
noise_type?: string;
db_window_ready?: boolean;
ambient_sustained_loud?: boolean;
is_spike?: boolean;
active_mode?: string;
active_scent?: number;
```

### 6. 앱 완료 기준

- 앱에서 소음 모드를 시작하면 화면에 소음 모드가 정상 표시된다.
- 앱을 재실행해도 서버가 ambient 상태이면 소음 모드가 복원된다.
- `spray: -1` 응답으로 전원이 꺼지거나 모드가 해제되지 않는다.
- 좋아요·싫어요 요청에 실제 `ambient_context`가 포함된다.
- 측정값이 없을 때 임의의 dB 숫자가 표시되지 않는다.

> 자체 BGM이 마이크 측정에 포함되는 문제는 이번 작업 범위에서 제외합니다.

---

## 하드웨어 개발자용 전달문

소음 분석 모드 서버 수정이 완료되었습니다. 펌웨어에서는 앱에서 시작한 소음 모드가 실제 `MODE_AMBIENT`로 전환되도록 아래 사항을 반영해 주세요.

### 1. 서버 ambient 모드 처리

`NetworkUI.cpp`의 서버 응답 처리에 다음 조건을 추가해 주세요.

```cpp
serverActiveMode == "ambient"
```

해당 응답을 받았을 때 현재 모드가 `MODE_AMBIENT`가 아니면 기존 출력을 안전하게 정리한 후 `MODE_AMBIENT`로 진입해야 합니다.

### 2. ambient 진입 상태 초기화

새 ambient 세션 진입 시 기존 시리얼 명령 `10`과 동일하게 다음 상태를 초기화해 주세요.

```cpp
ambientCycleCount = 0;
isFirstAmbientRun = true;
lastAmbientScent = 0;
isWaitingForTestDb = false;
forceAmbientSkip = false;
lastAmbientTime = 0;
```

초기화 후 다음 모드 전환을 수행해 주세요.

```cpp
setSystemMode(MODE_AMBIENT, "Ambient Mode");
```

### 3. 서버 `spray` 값 처리

서버 응답의 `spray` 값은 다음 의미를 가집니다.

- `spray > 0`: 새로운 분사 명령 실행
- `spray == -1`: 새 명령 없음. 현재 ambient 모드, 향기, 음악 유지
- `spray == 0`: 명시적인 정지

`spray == -1`을 정지로 처리하거나 기존 분사를 재시작하지 말아 주세요.

### 4. ambient 시작 처리 순서

`active_mode: "ambient"`와 `spray > 0`이 같이 오면 다음 순서로 처리해 주세요.

1. `MODE_AMBIENT` 진입
2. ambient 세션 상태 초기화
3. 서버가 지정한 향기 분사 실행
4. 이후 `runAmbientMode()`의 무음 측정 루프 실행

일반 분사만 실행하고 현재 모드가 READY 또는 MANUAL로 남으면 안 됩니다.

### 5. ambient 종료 조건

다음 경우에만 ambient 모드를 종료해 주세요.

- `active_mode == "ready"` 또는 `active_mode == "off"`
- 명시적인 `spray == 0`
- 사용자의 로컬 정지 명령
- 센서 오류 등 안전 정지

### 6. 소음값 단위

현재 펌웨어의 소음값은 실제 dB SPL이 아니라 PCM RMS 기반 상대 소음 레벨입니다.

실제 dB로 표시하려면 기준 음압계와 비교하여 마이크별 오프셋과 스케일을 보정하고, 보정값을 Preferences에 저장해야 합니다.

보정하지 않을 경우 펌웨어·앱·로그의 명칭을 `상대 소음 레벨`로 변경해 주세요.

펌웨어는 정상 측정 시 `microphone_valid=true`와 `db_level`을 함께 보내고, 초기화 실패·샘플 없음·오래된 샘플은 `microphone_valid=false`로 보내며 `db_level`을 생략합니다. 로컬 서버도 이 규약을 반영했으며 정상적인 `0dB`를 무효 측정과 구분합니다. 실제 Lambda 배포와 실물 확인은 별도입니다.

### 7. 하드웨어 완료 기준

- 앱에서 소음 모드를 시작하면 시리얼 입력 `10` 없이 `MODE_AMBIENT`로 진입한다.
- ambient 진입 후 `runAmbientMode()`가 실행된다.
- 서버의 `spray > 0` 명령이 ambient 모드 안에서 실행된다.
- `spray: -1` 응답으로 모드가 종료되거나 분사가 재시작되지 않는다.
- 앱에서 중지하면 `MODE_READY`로 정상 복귀한다.

> 자체 BGM이 마이크 측정에 포함되는 문제는 이번 작업 범위에서 제외합니다.

