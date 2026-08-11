#include "Globals.h"
#include <time.h>

// --- 컴파일 에러 해결을 위한 함수 사전 선언 ---
void processSettingModeInput();
void processStandardInput();

static const int NEXTION_DIM_NORMAL = 100;
static bool isDisplayDimmed = false;
static bool shouldShowStartupReadyPage = false;

// --- 로드셀 데이터 준비 완료 인터럽트 (ISR) ---
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
      // 음성 명령 비활성화됨
    } else if (eventCode == 2) { 
      wakeUpSystem();
    }
  }
}

// --- 서브 루틴: 스마트 절전 관리 ---
void manageSleepState() {
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

// 📄 [SystemLogic.cpp] initSystem 함수 전체 교체
void initSystem() {
  inputBuffer.reserve(64);
  lastWebMessage.reserve(128);
  lastWeatherRegion.reserve(32);
  blendSelection.reserve(8);

  Serial.begin(115200); Serial.setTimeout(5000);
  nexSerial.begin(9600, SERIAL_8N1, NEXTION_RX_PIN, NEXTION_TX_PIN);
  delay(300); 
  nexSend("sleep=0");
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
  
  pinMode(PIN_BUSY, INPUT_PULLUP);
  forceAllOff();

  Serial.printf(C_BOLD "\r\n 🚀 SMART DIFFUSER V12.7.0 (CLEAN SERVER SYNC) \r\n" C_RESET);

  mySoftwareSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  
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
  strip.setBrightness(150);
  setLedColor(0, 0, 0);
  calibration_factor = prefs.getFloat("cal_factor", 430.0);
  currentVolume = prefs.getInt("volume", 15);
  if (currentVolume < 0 || currentVolume > 30) {
    currentVolume = 15;
    prefs.putInt("volume", currentVolume);
  }
  myDFPlayer.volume(currentVolume);

  // ★ 펌웨어의 강제 개입 없이, 오직 서버에서 마지막으로 준 음악 데이터만 로드합니다.
  String savedMusic = prefs.getString("music_tracks", "1,6,11,16");
  updateMusicMapping(savedMusic);

  SprayIntensity(prefs.getInt("intensity", currentIntensity));

  for (int i = 0; i < 4; i++) {
    scales[i].begin(LOADCELL_DT[i], LOADCELL_SCK[i]);
    scales[i].set_scale(calibration_factor);
  
    String key = "off_" + String(i);
    long savedOffset = prefs.getLong(key.c_str(), 0);
  
    if (savedOffset != 0) {
      scales[i].set_offset(savedOffset);
      Serial.printf("[LoadCell %d] 오프셋 복구 완료: %ld\r\n", i + 1, savedOffset);
    } else {
      scales[i].tare();
    }
  
    lastNozzleSprayTime[i] = millis();

    if (i == 0) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[0]), isr_hx0, FALLING);
    if (i == 1) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[1]), isr_hx1, FALLING);
    if (i == 2) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[2]), isr_hx2, FALLING);
    if (i == 3) attachInterrupt(digitalPinToInterrupt(LOADCELL_DT[3]), isr_hx3, FALLING);
  }

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

  bool forceStartupPage = prefs.getBool("force_startup_page", false);
  if (forceStartupPage) {
    prefs.remove("force_startup_page");
    prefs.remove("pending_nextion_cmd");
    shouldShowStartupReadyPage = true;
  } else {
    String pendingNextionCmd = prefs.getString("pending_nextion_cmd", "");
    if (pendingNextionCmd.length() > 0) {
      prefs.remove("pending_nextion_cmd");
      handleNextionCmd(pendingNextionCmd);
    } else {
      shouldShowStartupReadyPage = true;
    }
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

  runScheduler();

  if (currentMode == MODE_AMBIENT) { 
    runAmbientMode(); 
    monitorWeight(); 
    pollServer();    
    checkSerialInput(); 
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

  vTaskDelay(WDT_YIELD_TIME_MS);
}

// --- 시리얼 입력 감시 ---
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
      resetWeightFilters();
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
        delay(50);
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
        if (currentMode == MODE_AMBIENT && isWaitingForTestDb) {
          int testDb = inputBuffer.toInt();
          Serial.printf("\r\n[Test] 강제 데시벨 %d dB 서버로 즉시 전송!\r\n", testDb);
          
          String payload = "{\"mode\": \"ambient\", \"db_level\": " + String(testDb) + ", \"weights\": [" +
                           String(weights[0], 1) + ", " + String(weights[1], 1) + ", " +
                           String(weights[2], 1) + ", " + String(weights[3], 1) + "]}";
          sendServerRequest(payload);
          
          isWaitingForTestDb = false;
          ambientCycleCount = 0;
          isFirstAmbientRun = false; 
          
          currentAmbientTrack++;
          if (currentAmbientTrack > 20) currentAmbientTrack = 1;
          playSound(currentAmbientTrack);
          Serial.printf(C_BLUE "🎵 노래 시작: %d번 트랙\r\n" C_RESET, currentAmbientTrack);
          lastAmbientTime = millis();
        
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
      if (currentMode == MODE_AMBIENT && c == 'n') {
          Serial.println("\r\n[Skip] n키 감지: 현재 노래를 중단하고 수음을 시작합니다.");
          myDFPlayer.stop();
          forceAmbientSkip = true;
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

// 서브 루틴: LED 설정 모드 입력 처리
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

// --- 통합 입력 처리 ---
void handleInput(String input) {
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
    requestWeatherRefresh(lastWeatherRegion);
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
    requestWeatherRefresh("");
  }
}

static const float MIN_SPRAY_SCENT_PERCENT = 15.0f;
static bool stopLowFluidActiveNozzles();

void runSprayLogic() { 
  updateClockDisplay();
  if (!isRunning) return;

  // ★ 1. 동료분의 쿨다운 아이디어 추가
  static unsigned long lastTrackChangeTime = 0;

  // 🎵 일반/수동 모드 플레이리스트 연속 재생 검사
  if (currentMode != MODE_AMBIENT && millis() - startTimeMillis > 3000) {
    
    // ★ 2. 트랙 변경 후 최소 0.5초(500ms)가 지나야만 BUSY 핀 검사
    if (millis() - lastTrackChangeTime > 500) {
      
      // ★ 3. Config:: 삭제 완료 (컴파일 에러 해결)
      if (digitalRead(PIN_BUSY) == HIGH) { 
        currentPlaylistIdx++;
        if (currentPlaylistIdx >= currentSlotTracksCount) {
          currentPlaylistIdx = 0; 
        }

        int nextTrack = currentSlotTracks[currentPlaylistIdx];
        lastTrackChangeTime = millis(); // 타이머 리셋

        myDFPlayer.stop();
        delay(100);
        myDFPlayer.playMp3Folder(nextTrack);
        
        Serial.printf(C_GREEN "\r\n🎵 [Playlist] 다음 곡 연속 재생 (%d/%d번째 곡): %d번 트랙 (%s)\r\n" C_RESET, 
                      currentPlaylistIdx + 1, currentSlotTracksCount, nextTrack, getTrackName(nextTrack).c_str());
        return;
      }
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
  Serial.println(" [i] 기기 ID 확인 및 변경 (예: iMyDevice_01)");
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
    if (millis() - lastTimeCheck < 60000) return;
    lastTimeCheck = millis();

    int h, m;
    getCurrentTime(h, m);
    if (h == -1) return;

    if (h == 8 && m == 5) {
        static int lastEventDay = -1;
        struct tm timeinfo;
        
        if (getLocalTime(&timeinfo)) {
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
            
            if (millis() - lowFluidTime[i] > 2000) {
                int pin = getPinFromCommand(i + 1);
                if (pin != -1) digitalWrite(pin, HIGH);
                activeNozzles[i] = false;
                blockedAnyNozzle = true;
                lowFluidTime[i] = 0;
                lastLowFluidCartridge = i + 1;
                Serial.printf(C_RED "\r\n🚨 [안전 정지] %d번 카트리지 잔량 %.1f%% 미만! 분사를 중지합니다.\r\n" C_RESET, i + 1, MIN_SPRAY_SCENT_PERCENT);
            } else {
                hasActiveNozzle = true;
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
            if (!activeNozzles[cmd - 1]) {
                activeNozzles[cmd - 1] = true;
                lastNozzleSprayTime[cmd - 1] = millis();
                activeCount++;
            }
        }
    }
    return activeCount;
}

void triggerSpray(int cmd, int dur, int music, String txt, bool isWeatherMode) {
  static int lastActiveCmd = -1;
  bool isAlreadyRunningSameCmd = (isRunning && lastActiveCmd == cmd);
  lastActiveCmd = cmd;

  forceAllOff();
  
  if (currentMode != MODE_AMBIENT && !isAlreadyRunningSameCmd) {
    myDFPlayer.stop();
  }
  
  isRunning = false;
  isSpraying = false;
  
  String cmdStr = String(cmd);
  int activeCount = parseAndSetNozzles(cmdStr);
  
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

  isRunning = true; 
  isSpraying = true; 
  sprayDuration = dur * 1000;
  prevMotorMillis = millis(); 
  
  for (int i = 0; i < 4; i++) {
    if (activeNozzles[i]) {
      int targetPin = getPinFromCommand(i + 1);
      if (targetPin != -1) digitalWrite(targetPin, LOW);
    }
  }
  
  // 🎵 오직 서버에서 내려준 음악 데이터만 믿고 재생하는 깔끔한 로직
  if (currentMode != MODE_AMBIENT && cmd >= 1 && cmd <= 34) {
    if (!isAlreadyRunningSameCmd) {
      currentSlotTracksCount = 0;
      currentPlaylistIdx = 0;
      startTimeMillis = millis();

      // ★ [우선순위 1] 단독 재생 로직(아이묭 버그 원인) 삭제! 무조건 플레이리스트를 파싱하도록 변경
      // [우선순위 2] 서버가 갱신해둔 해당 카트리지의 플레이리스트(slotPlaylists)를 읽어옵니다.
      for (int i = 0; i < cmdStr.length(); i++) {
        int cart = cmdStr.charAt(i) - '0';
        if (cart >= 1 && cart <= 4) {
          String rawSlotStr = slotPlaylists[cart - 1];
          rawSlotStr.trim();
          if (rawSlotStr.length() > 0 && rawSlotStr != "0") {
            int start = 0;
            while (currentSlotTracksCount < 10) {
              int comma = rawSlotStr.indexOf(',', start);
              String numStr = (comma == -1) ? rawSlotStr.substring(start) : rawSlotStr.substring(start, comma);
              numStr.trim();
              int trackNum = numStr.toInt();
              if (trackNum > 0) currentSlotTracks[currentSlotTracksCount++] = trackNum;
              if (comma == -1) break;
              start = comma + 1;
            }
          }
        }
      }

      // [우선순위 3] 서버에서 받은 배열마저 비어있다면 최후의 보루로 매핑된 기본 대표곡을 틉니다.
      if (currentSlotTracksCount == 0) {
        int primaryCart = (cmdStr.length() > 0) ? (cmdStr.charAt(0) - '0') : 1;
        if (primaryCart >= 1 && primaryCart <= 4) {
          currentSlotTracks[0] = musicMapping[primaryCart - 1];
        } else {
          currentSlotTracks[0] = 1; 
        }
        currentSlotTracksCount = 1;
      }
      
      // 서버가 결정한 최종 트랙 번호를 군말 없이 스피커 모듈로 넘깁니다.
      playSound(currentSlotTracks[0]);
      Serial.printf(C_GREEN "[Audio] 서버 데이터 기준 재생 시작 (총 %d곡 대기): %d번 트랙 (%s)\r\n" C_RESET, 
                    currentSlotTracksCount, currentSlotTracks[0], getTrackName(currentSlotTracks[0]).c_str());
    }
  }

  lastWebMessage = "성공: " + txt; 
  if (!isWeatherMode) {
    updateDisplay((cmd >= 10) ? 0 : cmd, txt);
  }

  Serial.printf("\r\n" C_GREEN "✅ 분사 실행: %s (명령:%d)\r\n" C_RESET, txt.c_str(), cmd);
  if (currentMode != MODE_SLEEP && !isWaitingForTestDb) {
      showPrompt();
  }
}

void updateMusicMapping(String data) {
    if (data.length() == 0) return;
    
    Serial.printf("\r\n🎵 음악 매핑 업데이트 수신: %s\r\n", data.c_str());
    
    int idx = 0;
    int start = 0;
    while (idx < 4) {
        int end = data.indexOf('_', start);
        String slotStr = (end == -1) ? data.substring(start) : data.substring(start, end);
        slotStr.trim();

        // 앱에서 전달받은 다중 트랙 문자열("22,23,24" 등)을 변형 없이 그대로 저장
        slotPlaylists[idx] = slotStr; 
        musicMapping[idx] = slotStr.toInt(); // 대표곡 번호
        
        if (end == -1) break;
        start = end + 1;
        idx++;
    }
    
    prefs.putString("music_tracks", data);
    Serial.println("💾 음악 매핑 및 플레이리스트 정보가 저장되었습니다.");
}

void runManualMode(String input) {
    triggerSpray(input.toInt(), 3, 0, "Manual Spray", false);
}
