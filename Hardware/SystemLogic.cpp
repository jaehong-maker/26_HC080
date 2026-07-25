#include "Globals.h"
#include <time.h>

// --- 컴파일 에러 해결을 위한 함수 사전 선언 (Forward Declarations) ---
void processSettingModeInput();
void processStandardInput();

static const int NEXTION_DIM_NORMAL = 100;
static bool isDisplayDimmed = false;
static bool shouldShowStartupReadyPage = false;

// --- 로드셀 데이터 준비 완료 인터럽트 (ISR) ---
// IRAM_ATTR을 붙여 인터럽트가 플래시 메모리가 아닌 RAM에서 최고 속도로 실행되게 합니다.
void IRAM_ATTR isr_hx0() { hxReady[0] = true; }
void IRAM_ATTR isr_hx1() { hxReady[1] = true; }
void IRAM_ATTR isr_hx2() { hxReady[2] = true; }
void IRAM_ATTR isr_hx3() { hxReady[3] = true; }

// --- 서브 루틴: 시스템 기상 ---
void wakeUpSystem() {
  if (!isDisplayDimmed && currentMode != MODE_SLEEP) return;
  lastActivityTime = millis();
  nexSend("dim=" + String(NEXTION_DIM_NORMAL));
  isDisplayDimmed = false;
  Serial.println(C_GREEN "\r\n☀️ 시스템 기상! 디스플레이 밝기 복구\r\n" C_RESET);

  if (currentMode == MODE_SLEEP) {
    setSystemMode(MODE_READY, "System Ready");
  }
}

// --- 서브 루틴: 오디오 큐 이벤트 처리 ---
void processAudioEvents() {
  int eventCode = 0;
  if (audioEventQueue != NULL && xQueueReceive(audioEventQueue, &eventCode, 0) == pdTRUE) {
    if (eventCode == 1) { 
      // 음성 명령(Voice Command) 기능 제거됨
    } else if (eventCode == 2) { 
      wakeUpSystem();
    }
  }
}

// --- 서브 루틴: 스마트 절전 관리 ---
void manageSleepState() {
  // 절전모드 비활성화: 디스플레이 밝기와 페이지를 자동으로 변경하지 않습니다.
}

// --- 서브 루틴: 향수 잔여량 경고 ---
void checkFluidLevels() {
  static unsigned long lastWarningTime = 0;
  if (currentMode == MODE_READY && (millis() - lastWarningTime > 10000)) { 
    lastWarningTime = millis();
    String warningMsg = "";
    for (int i = 0; i < 4; i++) {
      if (weights[i] > 0.5 && weights[i] < WEIGHT_THRESHOLD) {
        warningMsg += String(i + 1) + " ";
      }
    }
    if (warningMsg != "") {
      updateDisplay(0, "⚠️ Refill: " + warningMsg); 
      showPrompt();
    }
  }
}

// --- 서브 루틴: 노즐 막힘 방지 (Auto-Cleaning) ---
void runAutoCleaning() {
  if (isRunning || currentMode == MODE_SLEEP) return;
  unsigned long currentTime = millis();
  for (int i = 0; i < 4; i++) {
    if (currentTime - lastNozzleSprayTime[i] > CLEANING_INTERVAL) {
      Serial.printf(C_YELLOW "\r\n[Auto-Cleaning] %d번 노즐 청소 실행!\r\n" C_RESET, i + 1);
      triggerSpray(i + 1, 1, 0, "Nozzle Cleaning", false);
      break; 
    }
  }
}

// --- 시스템 초기화 ---
void initSystem() {
  // --- [메모리 최적화] 전역 String 객체 힙 단편화 방지 사전 할당 ---
  inputBuffer.reserve(64);
  lastWebMessage.reserve(128);
  lastWeatherRegion.reserve(32);
  blendSelection.reserve(8);

  Serial.begin(115200); Serial.setTimeout(5000);
  nexSerial.begin(9600, SERIAL_8N1, NEXTION_RX_PIN, NEXTION_TX_PIN);
  delay(300); 
  nexSend("sleep=0");  // ★ 추가: 부팅 시 디스플레이 수면 모드 강제 해제
  delay(100);
  nexSend("bauds=9600"); delay(100); nexSend("dim=" + String(NEXTION_DIM_NORMAL));
  delay(100);
  clearNextionInputBuffer();
  
  esp_task_wdt_config_t wdt_config = {
      .timeout_ms = 60000, 
      .idle_core_mask = (1 << 0) | (1 << 1), 
      .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config); 
  esp_task_wdt_add(NULL); 

  networkQueue = xQueueCreate(10, sizeof(String*));

  pinMode(PIN_SUNNY, OUTPUT); pinMode(PIN_CLOUDY, OUTPUT);
  pinMode(PIN_RAIN, OUTPUT); pinMode(PIN_SNOW, OUTPUT); pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUSY, INPUT);
  forceAllOff();

  Serial.printf(C_BOLD "\r\n 🚀 SMART DIFFUSER V12.7.0 (REFACTORED ARCHITECTURE) \r\n" C_RESET);

  mySoftwareSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  
  // 💡 [업그레이드] DFPlayer 하드웨어 리셋 대기(3초) 생략으로 초고속 부팅 구현 & 고장 바이패스
  if (!myDFPlayer.begin(mySoftwareSerial, true, false)) { 
    Serial.println(C_RED "⚠️ [Audio] 스피커 연결 불량! 무음 모드로 바이패스합니다." C_RESET);
  }

  initMicrophone();
  prefs.begin("diffuser", false);

  ledR = prefs.getUChar("ledR", 255);
  ledG = prefs.getUChar("ledG", 255);
  ledB = prefs.getUChar("ledB", 255);
  ledBrightness = prefs.getInt("ledBright", 150);
  ledEnabled = prefs.getBool("ledEnabled", true);
  lastWeatherRegion = prefs.getString("last_region", lastWeatherRegion);

  strip.begin();
  strip.setBrightness(150); // 너무 눈부시지 않게 150으로 제한 (0~255)
  setLedColor(0, 0, 0); // strip.show() 대신 우리가 만든 함수로 초기화! (앞쪽 4개 완벽 차단)
  calibration_factor = prefs.getFloat("cal_factor", 430.0);
  currentVolume = prefs.getInt("volume", 15);
  if (currentVolume < 0 || currentVolume > 30) {
    currentVolume = 15;
    prefs.putInt("volume", currentVolume);
  }
  myDFPlayer.volume(currentVolume);

  // 🎵 저장된 음악 매핑 복구
  String savedMusic = prefs.getString("music_tracks", "1_6_11_16");
  int idx = 0;
  int start = 0;
  while (idx < 4) {
    int end = savedMusic.indexOf('_', start);
    if (end == -1) {
      musicMapping[idx] = savedMusic.substring(start).toInt();
      break;
    }
    musicMapping[idx] = savedMusic.substring(start, end).toInt();
    start = end + 1;
    idx++;
  }

  SprayIntensity(prefs.getInt("intensity", currentIntensity));

  // initSystem() 함수 내부 로드셀 초기화 부분 수정
  for (int i = 0; i < 4; i++) {
    scales[i].begin(LOADCELL_DT[i], LOADCELL_SCK[i]);
    scales[i].set_scale(calibration_factor);
  
    // 1. 저장된 오프셋 키 생성 (예: "off_0", "off_1" 등)
    String key = "off_" + String(i);
  
    // 2. Preferences에서 저장된 오프셋 값 읽기 (기본값 0)
    long savedOffset = prefs.getLong(key.c_str(), 0);
  
    // 3. 읽어온 값이 있다면 적용 (부팅 시 자동 tare 방지)
    if (savedOffset != 0) {
      scales[i].set_offset(savedOffset);
      Serial.printf("[LoadCell %d] 오프셋 복구 완료: %ld\r\n", i + 1, savedOffset);
    } else {
      // 저장된 값이 없는 최초 실행 시에만 영점 잡기
      scales[i].tare();
    }
  
    lastNozzleSprayTime[i] = millis();

    // ★ [추가된 인터럽트 할당 로직] 
    // DOUT 핀이 HIGH에서 LOW로 떨어질 때(FALLING) 해당 센서의 ISR 실행
    if (i == 0) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[0]), isr_hx0, FALLING);
    if (i == 1) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[1]), isr_hx1, FALLING);
    if (i == 2) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[2]), isr_hx2, FALLING);
    if (i == 3) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[3]), isr_hx3, FALLING);
  }

  // 와이파이 연결 전에 시스템 자가 진단 실행
  checkSensorHealth();

  connectWiFi();
  configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  audioEventQueue = xQueueCreate(5, sizeof(int));
  networkEventGroup = xEventGroupCreate();
  initOTA(); 
  webServer.begin(); 
  
  bootAnimation();
  
  lastActivityTime = millis(); 
  setSystemMode(MODE_READY, "System Ready");
  updateScentProgressBars();

  String pendingNextionCmd = prefs.getString("pending_nextion_cmd", "");
  if (pendingNextionCmd.length() > 0) {
    prefs.remove("pending_nextion_cmd");
    handleNextionCmd(pendingNextionCmd);
  } else {
    shouldShowStartupReadyPage = true;
  }

  xTaskCreatePinnedToCore(networkTaskLoop, "NetworkTask", 8192, NULL, 1, &NetworkTaskHandle, 1);
  xTaskCreatePinnedToCore(sensorTaskLoop, "SensorTask", 8192, NULL, 1, &SensorTaskHandle, 0);
}

// --- 메인 루프 ---
void runSystem() {
  esp_task_wdt_reset(); 
  handleOTA(); 
  checkNextionInput(); 
  if (shouldShowStartupReadyPage) {
    shouldShowStartupReadyPage = false;
    clearNextionInputBuffer();
    showStartupReadyPage();
  }
  updateClockDisplay();

  // --- 스케줄러 추가 ---
  runScheduler();

  if (currentMode == MODE_AMBIENT) { 
    runAmbientMode(); 
    monitorWeight(); // 로드셀 센서 실시간 읽기 동작 허용
    pollServer();    // 1분 주기 서버 동기화 동작 허용
    checkSerialInput(); 
    // ★ 추가된 부분: 시스템(IDLE Task)에 CPU 제어권을 잠시 양보하여 워치독 에러 방지
    vTaskDelay(WDT_YIELD_TIME_MS);
    return; 
  }
  
  processAudioEvents();
  manageSleepState();

  if (currentMode == MODE_SLEEP) { 
    checkSerialInput(); 
    return; 
  } 

  manageWiFi(); 
  systemHeartbeat(); 
  handleWebClient(); 
  autoWeatherScheduler(); 
  pollServer(); 
  monitorWeight();
  
  runAutoCleaning(); 
  checkFluidLevels(); 

  if (currentMode == MODE_DEMO) runAutoDemoLoop(); 
  else if (isRunning) { runSprayLogic(); checkSafety(); } 

  checkSerialInput();

  // [추가] CPU 제어권을 시스템(IDLE Task)에 잠시 양보하여 워치독 트리거 방지
  vTaskDelay(WDT_YIELD_TIME_MS);
}

// --- 시리얼 입력 감시 (모듈화 적용) ---
void checkSerialInput() {
  if (Serial.available() <= 0) return;
  
  lastActivityTime = millis(); 
  if (currentMode == MODE_SLEEP) { wakeUpSystem(); return; }

  if (currentMode == MODE_REACTIVE) {
    char c = Serial.read(); if (c == '\n' || c == '\r') return;
    if (c == '+') { soundThreshold += 2; Serial.printf("\r\n🆙 감도: %d dB\r\n", soundThreshold); }
    else if (c == '-') { soundThreshold -= 2; Serial.printf("\r\n⬇️ 감도: %d dB\r\n", soundThreshold); }
    else if (c == '0') setSystemMode(MODE_READY, "Stopped");
    return;
  }

  if (currentMode == MODE_SETTING) {
    processSettingModeInput();
    return;
  }
  processStandardInput();

}

// --- 서브 루틴: 설정 모드 입력 처리 ---
void processSettingModeInput() {
  if (Serial.available() <= 0) return;
  char c = Serial.read(); 
  if (c == '\n' || c == '\r') return;

  // 💡 [수정] if-else 체인을 switch-case로 변경하여 직관성 확보
  switch (c) {
    case 'r':
      Serial.printf("\r\n[DEBUG] Scale 1 Raw: %ld\r\n", scales[0].read());
      break;

    case '+':
      calibration_factor += 10; 
      for(int i=0; i<4; i++) scales[i].set_scale(calibration_factor); 
      Serial.print("\r\n"); printCalibrationInfo(); showPrompt(); 
      break;

    case '-':
      calibration_factor -= 10; 
      for(int i=0; i<4; i++) scales[i].set_scale(calibration_factor); 
      Serial.print("\r\n"); printCalibrationInfo(); showPrompt(); 
      break;

    case 't':
      for(int i=0; i<4; i++) scales[i].tare(); 
      resetWeightFilters(); // ★ 추가: 지수 이동 평균의 이전 값을 즉시 0으로 리셋
      Serial.println("\r\n✅ [영점 완료] 필터 잔상이 제거되었습니다."); 
      Serial.printf("📊 현재 잔량: W1:%.1fg | W2:%.1fg | W3:%.1fg | W4:%.1fg\r\n", 
                    weights[0], weights[1], weights[2], weights[3]);
      showPrompt(); 
      break;

    case 's':
      prefs.putFloat("cal_factor", calibration_factor);
      for (int i = 0; i < 4; i++) {
        String key = "off_" + String(i);
        long currentOffset = scales[i].get_offset();
        prefs.putLong(key.c_str(), currentOffset);
      }
      Serial.println("\r\n💾 [저장 완료] 보정값 및 영점 오프셋이 기록되었습니다.");
      showPrompt();
      break;

    // SystemLogic.cpp 의 processSettingModeInput() 내 case 'p' 수정
    case 'p':
    {
        String payload = "{\"action\": \"POLL\", \"weights\": [";
        for(int i = 0; i < 4; i++) {
            float percent = calculateScentPercent(weights[i]);
            payload += String(percent, 1);
            if(i < 3) payload += ", ";
        }
        payload += "]}";
        Serial.println("\r\n🚀 [JSON 미리보기(%)]:\r\n" + payload); 
        showPrompt();
    }
    break;

    case 'i':
      {
        delay(50); // 버퍼 대기
        String newId = Serial.readString(); 
        newId.trim();
        if (newId.length() > 0) {
          deviceId = newId;
          prefs.putString("deviceId", deviceId);
          Serial.printf("\r\n💾 [저장 완료] 기기 ID가 '%s'(으)로 변경되었습니다.\r\n", deviceId.c_str());
        } else {
          Serial.printf("\r\nℹ️ 현재 기기 ID: %s\r\n", deviceId.c_str());
          Serial.println("변경하려면 'i' 뒤에 새 ID를 붙여서 입력하세요. (예: iSmartDiffuser_02)");
        }
        showPrompt();
      }
      break;

    case '0':
      setSystemMode(MODE_READY, "Main Menu");
      break;
  }
}

// --- 서브 루틴: 일반 모드 입력 처리 ---
void processStandardInput() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        
        // --- 테스트 모드(t) 데시벨 값 입력 처리 ---
        if (currentMode == MODE_AMBIENT && isWaitingForTestDb) {
          int testDb = inputBuffer.toInt();
          Serial.printf("\r\n[Test] 강제 데시벨 %d dB 서버로 즉시 전송!\r\n", testDb);
          
          // JSON 페이로드에 4채널 무게 배열(weights) 추가
          String payload = "{\"mode\": \"ambient\", \"db_level\": " + String(testDb) + ", \"weights\": [" +
                           String(weights[0], 1) + ", " + String(weights[1], 1) + ", " +
                           String(weights[2], 1) + ", " + String(weights[3], 1) + "]}";
          sendServerRequest(payload);
          
          isWaitingForTestDb = false;
          ambientCycleCount = 0; // 전송했으니 5주기 카운터 리셋
          isFirstAmbientRun = false; 
          
          currentAmbientTrack++;
          if (currentAmbientTrack > 20) currentAmbientTrack = 1;
          playSound(currentAmbientTrack);
          Serial.printf(C_BLUE "🎵 노래 시작: %d번 트랙\r\n" C_RESET, currentAmbientTrack);
          lastAmbientTime = millis(); // 쿨타임 시작
        
        // --- 일반 명령어 처리 ---
        } else if (inputBuffer == "0") {
          setSystemMode(MODE_READY, "Stopped");
        } else {
          handleInput(inputBuffer);
        }
        
        inputBuffer = "";
        if (currentMode != MODE_SLEEP && !isWaitingForTestDb) {
            showPrompt(); 
        }
      }
    } else if (c == '\b' || c == 0x7F) {
      if (inputBuffer.length() > 0) { inputBuffer.remove(inputBuffer.length() - 1); redrawInputLine(inputBuffer); }
    } else { 
      // --- 'n' 과 't' 단축키 처리 ---
      if (currentMode == MODE_AMBIENT && c == 'n') {
          Serial.println("\r\n[Skip] n키 감지: 현재 노래를 중단하고 수음을 시작합니다.");
          myDFPlayer.stop();
          forceAmbientSkip = true; // 안전하게 다음 루프에서 수음 발동
      } else if (currentMode == MODE_AMBIENT && c == 't') {
          Serial.println("\r\n[Test Mode] 수음을 건너뛰고 데시벨을 수동으로 입력합니다.");
          Serial.print("전송할 데시벨 값을 입력하고 엔터를 치세요 (예: 65) >> ");
          myDFPlayer.stop();
          isWaitingForTestDb = true; 
      } else {
          inputBuffer += c; 
          redrawInputLine(inputBuffer); 
      }
    }
  }
}

// 💡 [추가] 서브 루틴: LED 설정 모드 입력 처리 (handleInput에서 분리)
void processLedSettingInput(String input) {
  int values[4];
  int count = 0, lastIdx = 0;
  
  for (int i = 0; i < input.length() && count < 4; i++) {
    if (input.charAt(i) == ',') {
      values[count++] = input.substring(lastIdx, i).toInt();
      lastIdx = i + 1;
    }
  }
  if (count < 4) values[count++] = input.substring(lastIdx).toInt();

  if (count == 4) { 
    ledR = constrain(values[0], 0, 255);
    ledG = constrain(values[1], 0, 255);
    ledB = constrain(values[2], 0, 255);
    ledBrightness = constrain(values[3], 0, 255);
    ledEnabled = true;

    prefs.putUChar("ledR", ledR);
    prefs.putUChar("ledG", ledG);
    prefs.putUChar("ledB", ledB);
    prefs.putInt("ledBright", ledBrightness);
    prefs.putBool("ledEnabled", ledEnabled);

    Serial.printf("\r\n✅ LED 설정 완료: R:%d G:%d B:%d 밝기:%d\r\n", ledR, ledG, ledB, ledBrightness);
    setLedColor(ledR, ledG, ledB); 
    delay(1000);
    setSystemMode(MODE_READY, "Main Menu");
  } else {
    Serial.println("\r\n❌ 형식이 잘못되었습니다. 다시 입력해주세요.");
    showPrompt();
  }
}

// --- 통합 입력 처리 (최적화 완료) ---
void handleInput(String input) {
  // 분리된 LED 로직 호출
  if (currentMode == MODE_LED) {
    processLedSettingInput(input);
    return; 
  }
  
  if (currentMode == MODE_READY) {
    int cmdInt = input.toInt();
    switch (cmdInt) {
      case 1: setSystemMode(MODE_MANUAL, "Manual Mode"); printManualMenu(); showPrompt(); break;
      case 2: enterWeatherMode(false); break;
      case 3: setSystemMode(MODE_SETTING, "Setting Mode"); printSettingMenu(); showPrompt(); break;
      case 4: demoStep = 0; setSystemMode(MODE_DEMO, "Demo Mode"); break;
      case 5: Serial.println("\r\n[System] 음성 인식 기능이 비활성화되었습니다."); showPrompt(); break;
      case 6: setSystemMode(MODE_VISUAL, "Visualizer"); break;
      case 7: setSystemMode(MODE_REACTIVE, "Sound Reaction"); break;
      case 8: printDashboard(); break;
      case 9: 
        setSystemMode(MODE_LED, "LED Setting");
        Serial.println("\r\n🎨 [LED 설정] R, G, B, 밝기 값을 쉼표로 구분하여 입력하세요.");
        Serial.println("  (예: 255,255,255,150) ※ 밝기 범위: 0(꺼짐) ~ 255(최대)");
        Serial.println("  (취소하고 돌아가려면 0 입력)");
        showPrompt();
        break;
      case 10: 
        stopSystem();
        ambientCycleCount = 0;
        isFirstAmbientRun = true; 
        lastAmbientScent = 0;
        isWaitingForTestDb = false;
        forceAmbientSkip = false;
        lastAmbientTime = 0; 
        setSystemMode(MODE_AMBIENT, "Ambient Mode"); 
        break;
    }
  } 
  else if (currentMode == MODE_MANUAL) {
    if (input == "+") { changeVolume(currentVolume + 2); showPrompt(); }
    else if (input == "-") { changeVolume(currentVolume - 2); showPrompt(); }
    else runManualMode(input); 
  }
  else if (currentMode == MODE_WEATHER) {
    rememberWeatherRegion(input);
    Serial.printf("\r\n🔍 [%s] 지역 날씨 조회 요청 중...\r\n", input.c_str());
    lastWeatherCallMillis = millis();
    sendServerRequest(buildWeatherRequestPayload(lastWeatherRegion));
  }
}

void enterWeatherMode(bool runNow) {
  setSystemMode(MODE_WEATHER);
  updateDisplay(lastWeatherIconId, "");
  Serial.println(C_YELLOW "👉 지역명을 입력하세요" C_RESET);
  if (lastWeatherRegion.length() > 0) {
    Serial.printf(C_YELLOW "\r\n[Weather] Saved region: %s\r\n" C_RESET, lastWeatherRegion.c_str());
  }
  if (runNow) {
    lastWeatherCallMillis = millis();
    sendServerRequest(buildWeatherRequestPayload(""));
  }
}

static const float MIN_SPRAY_SCENT_PERCENT = 15.0f;
static bool stopLowFluidActiveNozzles();

void runSprayLogic() { 
  updateClockDisplay();
  // 💡 [업그레이드] 불필요한 for문 반복을 제거하는 Early Return(빠른 종료) 최적화
  // 시스템이 분사 중이 아닐 때는 연산을 즉시 건너뛰어 CPU 리소스를 절약합니다.
  if (!isRunning) return;

  if (currentMode != MODE_AMBIENT && millis() - startTimeMillis > 4000) {
    // 재생 중인 음악이 끝났는지 확인 (DFPlayer BUSY 핀 상태 확인)
    if (digitalRead(Config::PIN_DF_BUSY) == HIGH) {
      Serial.println(C_GREEN "\r\n🎵 [Audio] 노래 재생이 완료되어 시스템을 정지합니다." C_RESET);
      stopSystem();
      updateDisplay(0, "Music Finished");
      return;
    }
  }

  if (stopLowFluidActiveNozzles()) return;

  if (isSpraying) {
    if (millis() - prevMotorMillis >= sprayDuration) {
      forceAllOff();
      isSpraying = false; 
      prevMotorMillis = millis();
    }
  } else {
    if (millis() - prevMotorMillis >= REST_TIME) {
      if (stopLowFluidActiveNozzles()) return;
      forceAllOff(); 
      for (int i = 0; i < 4; i++) {
        if (activeNozzles[i]) {
          int pin = getPinFromCommand(i + 1);
          if (pin != -1) digitalWrite(pin, LOW);
        }
      }
      isSpraying = true; 
      prevMotorMillis = millis();
    }
  }
}

void runAutoDemoLoop() { 
  if (millis() - prevDemoMillis >= 4000) {
    prevDemoMillis = millis(); forceAllOff(); demoStep++;
    if (demoStep > 4) demoStep = 1;
    int targetPin = getPinFromCommand(demoStep);
    if (targetPin != -1) {
      digitalWrite(targetPin, LOW);
      lastNozzleSprayTime[demoStep - 1] = millis(); 
    }
    playSound(demoStep); updateDisplay(demoStep, "Demo Mode");
  }
}

void checkSafety() {
  if (millis() - startTimeMillis > MAX_RUN_TIME) { setSystemMode(MODE_READY, "Safety Timeout"); }
}

void setSystemMode(SystemMode mode, String msg) { 
  currentMode = mode;
  if (msg != "") {
    Serial.printf(C_CYAN "\r\n[Mode] %s\r\n" C_RESET, msg.c_str());
  }
  
  if (mode == MODE_READY) {
    stopSystem();
    printMainMenu(); 
  }
}

void printSettingMenu() {
  Serial.println("\r\n" C_YELLOW "--- [설정 모드 명령어 가이드] ---" C_RESET);
  Serial.println(" [+] 로드셀 보정값 10 증가");
  Serial.println(" [-] 로드셀 보정값 10 감소");
  Serial.println(" [t] 저울 영점 조절 (Tare)");
  Serial.println(" [s] 현재 보정값 저장 (Save to Flash)");
  Serial.println(" [p] 서버 전송용 JSON 데이터 미리보기");
  Serial.println(" [i] 기기 ID 확인 및 변경 (예: iMyDevice_01)"); // ★ 추가
  Serial.println(" [0] 메인 메뉴로 돌아가기");
  Serial.println(C_YELLOW "--------------------------------" C_RESET);
}

void printManualMenu() {
  Serial.println("\r\n" C_YELLOW "--- [수동 모드 명령어 가이드] ---" C_RESET);
  Serial.println(" [1-4] 단일 분사구 작동 (예: 1)");
  Serial.println(" [혼합] 번호 연속 입력 시 시그니처 향 분사 (예: 13, 24)");
  Serial.println(" [+] 스피커 볼륨 2 증가");
  Serial.println(" [-] 스피커 볼륨 2 감소");
  Serial.println(" [0] 메인 메뉴로 돌아가기");
  Serial.println(C_YELLOW "--------------------------------" C_RESET);
}

// 현재 시간을 가져오는 유틸리티 함수
void getCurrentTime(int &hour, int &minute) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        hour = -1; minute = -1;
        return;
    }
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
}

void runScheduler() {
    if (!schedulerEnabled || WiFi.status() != WL_CONNECTED) return;

    static unsigned long lastTimeCheck = 0;
    if (millis() - lastTimeCheck < 60000) return; // 1분마다 체크
    lastTimeCheck = millis();

    int h, m;
    getCurrentTime(h, m);
    if (h == -1) return; // 시간 획득 실패 시 종료

    // 절전모드 비활성화: 운영 시간 밖이어도 디스플레이를 자동으로 어둡게 하지 않습니다.

    // 3. 특정 시간 '모닝 향기' 이벤트 (예: 아침 8시 5분)
    if (h == 8 && m == 5) {
        static int lastEventDay = -1; // 실행된 날짜를 기억 (함수 밖으로 나가도 유지됨)
        struct tm timeinfo;           // 시간 정보를 담을 구조체 선언
        
        if (getLocalTime(&timeinfo)) { // 현재 날짜 정보를 가져옴
            if (lastEventDay != timeinfo.tm_mday) {
                lastEventDay = timeinfo.tm_mday;
                Serial.println(C_MAGENTA "\r\n[Scheduler] Good Morning! 아침 향기를 분사합니다.\r\n" C_RESET);
                triggerSpray(1, 5, 1, "Morning Scent", false);
            }
        }
    }
}

void resetScaleZero() {
  Serial.println(C_YELLOW "\r\n[Calibration] Resetting load cell zero offsets..." C_RESET);

  forceAllOff();
  isRunning = false;
  isSpraying = false;

  for (int i = 0; i < 4; i++) {
    scales[i].tare();

    String key = "off_" + String(i);
    long currentOffset = scales[i].get_offset();
    prefs.putLong(key.c_str(), currentOffset);

    Serial.printf(C_GREEN "[Calibration] Load cell %d zeroed (Offset: %ld)\r\n" C_RESET, i + 1, currentOffset);
    weights[i] = 0.0f;
  }

  updateScentProgressBars();
  updateDisplay(0, "Tare Complete!");
  Serial.println(C_CYAN "[Calibration] Zero reset complete." C_RESET);
}

// --- [링커 에러 해결을 위한 연결 함수들] ---

/**
 * @brief 기존 코드와의 호환성을 위해 유지하는 노즐 파싱 함수
 * NetworkUI.cpp의 triggerSpray 등에서 사용합니다.
 */
static bool stopLowFluidActiveNozzles() {
    bool hasActiveNozzle = false;
    bool blockedAnyNozzle = false;
    static unsigned long lowFluidTime[4] = {0, 0, 0, 0};
    int lastLowFluidCartridge = -1;

    for (int i = 0; i < 4; i++) {
        if (!activeNozzles[i]) {
            lowFluidTime[i] = 0;
            continue;
        }

        float percent = calculateScentPercent(weights[i]);
        if (percent < MIN_SPRAY_SCENT_PERCENT) {
            if (lowFluidTime[i] == 0) lowFluidTime[i] = millis();
            
            if (millis() - lowFluidTime[i] > 2000) { // 2초 이상 지속되어야 진짜 부족한 것으로 판별
                int pin = getPinFromCommand(i + 1);
                if (pin != -1) digitalWrite(pin, HIGH);
                activeNozzles[i] = false;
                blockedAnyNozzle = true;
                lowFluidTime[i] = 0;
                lastLowFluidCartridge = i + 1;
                Serial.printf(C_RED "\r\n🚨 [안전 정지] %d번 카트리지 잔량 %.1f%% 미만! 분사를 중지합니다.\r\n" C_RESET, i + 1, MIN_SPRAY_SCENT_PERCENT);
            } else {
                hasActiveNozzle = true; // 대기 중 (디바운스)
            }
        } else {
            lowFluidTime[i] = 0;
            hasActiveNozzle = true;
        }
    }

    if (blockedAnyNozzle) {
        updateScentProgressBars();
    }

    if (!hasActiveNozzle) {
        forceAllOff();
        myDFPlayer.stop();
        isRunning = false;
        isSpraying = false;
        blendSelection = "";
        blendSprayActive = false;
        updateDisplay(0, "Low Fluid! Stopped");
        showLowFluidPage(lastLowFluidCartridge);
        return true;
    }

    return false;
}

int parseAndSetNozzles(String cmdStr) {
    int activeCount = 0;
    for (int i = 0; i < 4; i++) activeNozzles[i] = false;

    for (int i = 0; i < (int)cmdStr.length(); i++) {
        int cmd = cmdStr.charAt(i) - '0';
        if (cmd >= 1 && cmd <= 4) {
            // 시작 전 즉시 차단 로직 제거 (오탐 방지). 
            // 실제 부족 여부는 분사 시작 후 2초 디바운스를 거쳐 stopLowFluidActiveNozzles()에서 판단합니다.
            if (!activeNozzles[cmd - 1]) {
                activeNozzles[cmd - 1] = true;
                lastNozzleSprayTime[cmd - 1] = millis();
                activeCount++;
            }
        }
    }
    return activeCount;
}

// =================================================================
// 🚿 [통합 분사 제어 컨트롤러] 모든 분사(수동, 서버, 날씨, 음성) 전담
// =================================================================
void triggerSpray(int cmd, int dur, int music, String txt, bool isWeatherMode) {
  // 1. 모터 끄기 (기존 분사 중단)
  forceAllOff();
  
  // ★ 앰비언트 모드가 아닐 때만 BGM을 끕니다. (앰비언트 BGM 연속성 보장)
  if (currentMode != MODE_AMBIENT) {
    myDFPlayer.stop();
  }
  
  isRunning = false;
  isSpraying = false;
  
  // 2. 명령어 파싱
  String cmdStr = String(cmd);
  int activeCount = parseAndSetNozzles(cmdStr);
  
  // 3. 15% 미만 잔량 노즐 개별 필터링
  bool hasValidNozzle = false;
  bool blockedLowFluidNozzle = false;
  int lastLowFluidCartridge = -1;
  for (int i = 0; i < 4; i++) {
      if (activeNozzles[i]) {
          if (calculateScentPercent(weights[i]) < MIN_SPRAY_SCENT_PERCENT) {
              Serial.printf(C_YELLOW "\r\n[Warning] %d번 향기 잔량이 15%% 미만이어서 작동에서 제외됩니다.\r\n" C_RESET, i + 1);
              activeNozzles[i] = false;
              blockedLowFluidNozzle = true;
              lastLowFluidCartridge = i + 1;
          } else {
              hasValidNozzle = true;
          }
      }
  }

  if (blockedLowFluidNozzle) {
      updateScentProgressBars();
      showLowFluidPage(lastLowFluidCartridge);
  }

  if (!hasValidNozzle) {
      Serial.println(C_YELLOW "⚠️ 작동 가능한 카트리지가 없어 분사가 취소되었습니다." C_RESET);
      return; 
  }

  // 4. 시스템 분사 상태 ON 및 타이머 설정
  isRunning = true; 
  isSpraying = true; 
  sprayDuration = dur * 1000;
  prevMotorMillis = millis(); 
  startTimeMillis = millis(); 
  
  // 5. 모터(펌프) 하드웨어 핀 작동
  for (int i = 0; i < 4; i++) {
    if (activeNozzles[i]) {
      int targetPin = getPinFromCommand(i + 1);
      if (targetPin != -1) digitalWrite(targetPin, LOW);
    }
  }
  
  // 6. 수동/날씨 모드 등 일반 모드일 때만 지정된 트랙 재생
  if (currentMode != MODE_AMBIENT) {
    int track = 1;
    if (music > 0 && music <= 25) {
        track = music;
    } else {
        if (cmd >= 1 && cmd <= 4) {
            track = musicMapping[cmd - 1];
        } else {
            track = 1;
        }
    }
    playSound(track);
    Serial.printf(C_GREEN "[Audio] 재생 시도: %d번 트랙 (%s)\r\n" C_RESET, track, getTrackName(track).c_str());
  }

  // 7. 디스플레이 및 웹 로그 업데이트
  lastWebMessage = "성공: " + txt; 
  if (!isWeatherMode) {
    updateDisplay((cmd >= 10) ? 0 : cmd, txt);
  }

  Serial.printf("\r\n" C_GREEN "✅ 분사 실행: %s (명령:%d)\r\n" C_RESET, txt.c_str(), cmd);
  if (currentMode != MODE_SLEEP && !isWaitingForTestDb) {
      showPrompt();
  }
}

/**
 * @brief 서버에서 받은 음악 매핑 데이터를 파싱하고 저장합니다.
 */
void updateMusicMapping(String data) {
    if (data.length() == 0) return;
    
    Serial.printf("\r\n🎵 음악 매핑 업데이트 수신: %s\r\n", data.c_str());
    
    int idx = 0;
    int start = 0;
    while (idx < 4) {
        int end = data.indexOf('_', start);
        if (end == -1) {
            musicMapping[idx] = data.substring(start).toInt();
            break;
        }
        musicMapping[idx] = data.substring(start, end).toInt();
        start = end + 1;
        idx++;
    }
    
    // Preferences에 저장
    prefs.putString("music_tracks", data);
    Serial.println("💾 음악 매핑 정보가 저장되었습니다.");
}

/**
 * @brief 기존 코드와의 호환성을 위해 유지하는 수동 모드 실행 함수
 */
void runManualMode(String input) {
    // 수동 명령어(1~4, 13 등)를 통합 컨트롤러로 릴레이 (기본 3초 분사)
    triggerSpray(input.toInt(), 3, 0, "Manual Spray", false);
}
