# 소음 분석 모드 개발자 전달사항

## 최초 측정 중 분사 정책 (테스트용 60초)

- 최초 ambient 진입 시 서버가 현재 소음에 맞는 장착 향을 선택하고 `duration=60` 명령을 한 번 저장합니다.
- 클라이언트가 `duration=3`을 보내도 최초 명령에는 서버의 `AMBIENT_INITIAL_DURATION_SECONDS`를 적용합니다.
- 초기 명령 저장과 함께 기존 소음 샘플, 측정 준비 상태 및 이전 자동 정지 타이머를 초기화합니다.
- 음악·LED·분사 명령은 기기의 후속 POLL로 전달합니다. 명령 소비 후에는 `spray=-1`, `app_command=false`를 반환합니다.
- 최초 측정 중 중복 시작 요청은 진행 중인 분사 시간을 갱신하지 않습니다.
- 이후 최근 60초 평균이 Quiet(<60), Moderate(60~79), Loud(>=80)의 다른 구간으로 바뀌면 3초 1회 분사합니다. 측정 완료 자체로는 재분사하지 않습니다.
- 이 변경은 서버 코드에만 적용합니다. 펌웨어의 직접 추천 응답을 진단 전용으로 바꾸는 요구사항은 별도이며, 현재 펌웨어 처리는 수정하지 않습니다.
- GitHub 업로드와 실제 Lambda 배포·실물 검증은 구분해야 합니다.

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

마이크 측정 실패도 정상적인 `0dB`와 구분할 수 있도록 유효 상태 필드를 별도로 전달하는 방식을 권장합니다.

### 7. 하드웨어 완료 기준

- 앱에서 소음 모드를 시작하면 시리얼 입력 `10` 없이 `MODE_AMBIENT`로 진입한다.
- ambient 진입 후 `runAmbientMode()`가 실행된다.
- 서버의 `spray > 0` 명령이 ambient 모드 안에서 실행된다.
- `spray: -1` 응답으로 모드가 종료되거나 분사가 재시작되지 않는다.
- 앱에서 중지하면 `MODE_READY`로 정상 복귀한다.

> 자체 BGM이 마이크 측정에 포함되는 문제는 이번 작업 범위에서 제외합니다.

