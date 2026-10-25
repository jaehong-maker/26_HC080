#include "Globals.h"

// ===================================================================================
// [1] 유틸리티 (수학 계산 및 정렬 알고리즘)
// ===================================================================================

static int calculateDecibels(int32_t* samples, int numSamples) {
  if (numSamples <= 0) return 0;
  
  float sumSquares = 0.0f;
  for (int i = 0; i < numSamples; i++) {
    int16_t wave = (samples[i] >> 14);
    sumSquares += (float)wave * wave;
  }
  
  float rms = sqrt(sumSquares / numSamples);
  if (rms < 1.0f) rms = 1.0f; 
  
  return (int)(20.0f * log10(rms));
}

float calculateStdDev(int* samples, int count, float average) {
    float sumVariance = 0;
    for (int i = 0; i < count; i++) {
        sumVariance += pow(samples[i] - average, 2);
    }
    return sqrt(sumVariance / count);
}

// ===================================================================================
// [2] LED 애니메이션 및 시각적 피드백
// ===================================================================================

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  strip.setBrightness(ledBrightness); 
  
  for(int i = 0; i < NUM_LEDS; i++) {
    // 인덱스 0, 1, 2, 3 (처음 4개)는 강제로 끕니다 (R:0, G:0, B:0)
    if (i < 4) {
      strip.setPixelColor(i, strip.Color(0, 0, 0));
    } 
    // 나머지 8개(인덱스 4~11)는 정상적으로 색상을 켭니다
    else {
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
  }
  
  strip.show();
}

void bootAnimation() {
  for (int i = 0; i < 3; i++) {
    setLedColor(ledR, ledG, ledB); 
    delay(100);
    setLedColor(0, 0, 0); 
    delay(100);
  }
}

void systemHeartbeat() {
  if (!ledEnabled) {
    setLedColor(0, 0, 0);
    return;
  }

  unsigned long currentTime = millis();
  static unsigned long lastUpdate = 0;
  static int effectStep = 0;
  static uint16_t hue = 0;

  // 1. 상태 체크: 센서 고장 및 잔량 부족 확인
  bool isAnySensorBroken = !isAudioOk; 
  bool isFluidLow = false;

  for (int i = 0; i < 4; i++) {
    if (!isSensorOk[i]) isAnySensorBroken = true;
    if (weights[i] > 0.5f && weights[i] < (Config::EMPTY_WEIGHT + 2.0f)) isFluidLow = true;
  }

  // 2. 현재 시스템 상태에 따라 LED 상태 결정 (특수 상태 우선)
  if (isAnySensorBroken) {
    if (currentTime - lastUpdate >= 300) { 
      lastUpdate = currentTime; effectStep = !effectStep;
      setLedColor(effectStep ? 255 : 0, 0, 0); 
    }
    return;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    if (currentTime - lastUpdate >= 600) {
      lastUpdate = currentTime; effectStep = !effectStep;
      setLedColor(0, 0, effectStep ? 255 : 0); 
    }
    return;
  }

  // 3. 앱에서 설정한 LED 효과 모드 실행
  switch (ledEffect) {
    case LED_BREATHE: // 〰 호흡 효과
      {
        int factor = (sin(currentTime / 1000.0) * 45) + 210; 
        setLedColor((ledR * factor) / 255, (ledG * factor) / 255, (ledB * factor) / 255);
      }
      break;

    case LED_RAINBOW: // 🌈 무지개 효과
      {
        if (currentTime - lastUpdate >= 20) {
          lastUpdate = currentTime;
          hue += 256;
          for(int i=0; i<NUM_LEDS; i++) {
            if (i >= 4) {
              strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue + (i * 65536 / NUM_LEDS))));
            } else {
              strip.setPixelColor(i, 0);
            }
          }
          strip.setBrightness(ledBrightness);
          strip.show();
        }
      }
      break;

    case LED_MUSIC: // 🎵 음악 반응 효과
      {
        int vol = currentDbLevel; 
        int sensitivity = 70; // 소음 기준값
        int factor = map(constrain(vol, sensitivity, 100), sensitivity, 100, 50, 255);
        setLedColor((ledR * factor) / 255, (ledG * factor) / 255, (ledB * factor) / 255);
      }
      break;

    case LED_SOLID: // ● 단색 유지
    default:
      setLedColor(ledR, ledG, ledB);
      break;
  }

  // 4. 동작 중(분사 등)일 때 추가 시각 효과 (필요 시)
  if (isSpraying || isRunning) {
    // 분사 중에는 색상을 조금 더 밝게 하거나 살짝 떨리는 효과를 줄 수 있음
  }
}

// ===================================================================================
// [3] 하드웨어 출력 제어 및 자가 진단
// ===================================================================================

void forceAllOff() {
    for (int i = 0; i < 4; i++) {
        digitalWrite(Config::SPRAY_PINS[i], HIGH);
    }
}

void applyNozzleHardwareState() {
    for (int i = 0; i < 4; i++) {
        if (isSpraying && activeNozzles[i]) enableNozzleIfSensorValid(i);
        else digitalWrite(Config::SPRAY_PINS[i], HIGH);
    }
}

void stopSystem() {
  blendSelection = ""; blendSprayActive = false;
  nexSend("b_blend.txt=\"blendON\"");
  blendModeEnabled = false;
  
  forceAllOff();
  isRunning = false; isSpraying = false;
  for (int i = 0; i < 4; i++) activeNozzles[i] = false;
  
  myDFPlayer.stop();
}

void resetManualState() {
  forceAllOff();
  myDFPlayer.stop();
  isRunning = false; isSpraying = false; sprayDuration = 0;
  prevMotorMillis = 0; startTimeMillis = 0;
  for (int i = 0; i < 4; i++) activeNozzles[i] = false;
  blendModeEnabled = false; blendSelection = ""; blendSprayActive = false;
  nexSend("b_blend.txt=\"blendON\"");
}

void checkSensorHealth() {
    Serial.println(C_YELLOW "\r\n[Diagnosis] 시스템 자가 진단 시작..." C_RESET);

    isAudioOk = true; 
    Serial.println(C_GREEN "✅ [Audio] 스피커 연결 확인 (호환 칩 응답 무시 적용)" C_RESET);

    unsigned long diagnosisStart = millis();
    while (millis() - diagnosisStart < 1000UL) {
        monitorWeight();
        esp_task_wdt_reset();
        delay(5);
    }
    for (int i = 0; i < 4; i++) {
        if (!isSensorOk[i]) Serial.printf(C_RED "⚠️ [Warning] 로드셀 %d번 연결 불량!\r\n" C_RESET, i + 1);
        else Serial.printf(C_GREEN "✅ [LoadCell %d] 연결 확인\r\n" C_RESET, i + 1);
    }
    Serial.println(C_GREEN "✅ 자가 진단 완료." C_RESET);
}

// ===================================================================================
// [4] 무게 센서 (로드셀) 데이터 처리
// ===================================================================================

namespace {
constexpr uint8_t ALL_LOADCELLS = 0x0f;
constexpr unsigned long READY_WAIT_MS = 250;
constexpr unsigned long SAMPLE_MAX_AGE_MS = 1500;
constexpr unsigned long TARE_TIMEOUT_MS = 4000;
constexpr int TARE_SAMPLES = 10;
portMUX_TYPE loadcellStateMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE loadcellClockMux = portMUX_INITIALIZER_UNLOCKED;
bool loadcellsInitialized = false;
bool loadcellCalibrating = false;
bool offsetKnown[4] = {};
bool sampleSeen[4] = {};
bool sensorFaultLatched[4] = {};
bool firstWeight[4] = {true, true, true, true};
unsigned long lastSampleMillis[4] = {};
bool waitingForReady = false;
unsigned long readyWaitStart = 0;

bool measurementValidLocked(int i, unsigned long now) {
  return loadcellsInitialized && !loadcellCalibrating && offsetKnown[i] &&
         !sensorFaultLatched[i] && sampleSeen[i] && isSensorOk[i] &&
         now - lastSampleMillis[i] <= SAMPLE_MAX_AGE_MS;
}

void latchSensorFault(int i) {
  portENTER_CRITICAL(&loadcellStateMux);
  sensorFaultLatched[i] = true;
  isSensorOk[i] = false;
  digitalWrite(Config::SPRAY_PINS[i], HIGH);
  portEXIT_CRITICAL(&loadcellStateMux);
  weights[i] = 0.0f;
  firstWeight[i] = true;
}

void resetLoadcellBus() {
  // A shared power-down/reset realigns all four ADCs to channel A, gain 128.
  digitalWrite(LOADCELL_SCK[0], HIGH);
  delayMicroseconds(80);
  digitalWrite(LOADCELL_SCK[0], LOW);
  waitingForReady = false;
}

uint8_t readLoadcellFrame(int32_t raw[4]) {
  uint8_t ready = 0;
  for (int i = 0; i < 4; ++i) {
    raw[i] = 0;
    if (digitalRead(LOADCELL_DT[i]) == LOW) ready |= (1U << i);
  }
  if (!ready) { waitingForReady = false; return 0; }
  if (!waitingForReady) { waitingForReady = true; readyWaitStart = millis(); }
  if (ready != ALL_LOADCELLS && millis() - readyWaitStart < READY_WAIT_MS) return 0;
  waitingForReady = false;

  // Never call HX711::read/get_units/tare individually on this shared clock.
  // Keep each high pulse below 50 us, including protection from interrupts.
  portENTER_CRITICAL(&loadcellClockMux);
  for (int bit = 0; bit < 24; ++bit) {
    digitalWrite(LOADCELL_SCK[0], HIGH);
    delayMicroseconds(1);
    for (int i = 0; i < 4; ++i) raw[i] = (raw[i] << 1) | digitalRead(LOADCELL_DT[i]);
    digitalWrite(LOADCELL_SCK[0], LOW);
    delayMicroseconds(1);
  }
  digitalWrite(LOADCELL_SCK[0], HIGH); // Pulse 25: next conversion A / gain 128.
  delayMicroseconds(1);
  digitalWrite(LOADCELL_SCK[0], LOW);
  delayMicroseconds(1);
  uint8_t completed = 0;
  for (int i = 0; i < 4; ++i) {
    if (digitalRead(LOADCELL_DT[i]) == HIGH) completed |= (1U << i);
  }
  portEXIT_CRITICAL(&loadcellClockMux);

  uint8_t valid = ready & completed;
  for (int i = 0; i < 4; ++i) {
    if (raw[i] & 0x800000) raw[i] -= 0x1000000;
    if (raw[i] == -8388608 || raw[i] == 8388607) valid &= ~(1U << i);
    // A channel that missed shared clocks must be reset/tared before reuse.
    if (!(valid & (1U << i))) latchSensorFault(i);
  }
  return valid;
}

uint8_t calibrateLoadcells(uint8_t requested, bool persist) {
  stopSystem();
  portENTER_CRITICAL(&loadcellStateMux);
  loadcellCalibrating = true;
  for (int i = 0; i < 4; ++i) { sampleSeen[i] = false; isSensorOk[i] = false; }
  portEXIT_CRITICAL(&loadcellStateMux);
  resetLoadcellBus();
  int64_t sums[4] = {};
  uint8_t counts[4] = {};
  uint8_t success = 0;
  unsigned long started = millis();
  while (millis() - started < TARE_TIMEOUT_MS && (success & requested) != requested) {
    int32_t raw[4];
    uint8_t valid = readLoadcellFrame(raw);
    for (int i = 0; i < 4; ++i) {
      if ((requested & valid & (1U << i)) && counts[i] < TARE_SAMPLES) {
        sums[i] += raw[i];
        if (++counts[i] == TARE_SAMPLES) success |= (1U << i);
      }
    }
    esp_task_wdt_reset();
    delay(1);
  }
  for (int i = 0; i < 4; ++i) {
    if (!(requested & (1U << i))) continue;
    bool ok = success & (1U << i);
    if (ok) {
      long offset = (long)(sums[i] / TARE_SAMPLES);
      scales[i].set_offset(offset);
      if (persist) prefs.putLong(("off_" + String(i)).c_str(), offset);
    }
    portENTER_CRITICAL(&loadcellStateMux);
    offsetKnown[i] = ok;
    sensorFaultLatched[i] = !ok;
    portEXIT_CRITICAL(&loadcellStateMux);
    Serial.printf("[LoadCell %d] tare %s\r\n", i + 1, ok ? "OK" : "TIMEOUT / INVALID");
  }
  resetWeightFilters();
  portENTER_CRITICAL(&loadcellStateMux);
  loadcellCalibrating = false;
  portEXIT_CRITICAL(&loadcellStateMux);
  return success;
}
} // namespace

void initializeLoadcells() {
  uint8_t missing = 0;
  for (int i = 0; i < 4; ++i) {
    scales[i].begin(LOADCELL_DT[i], LOADCELL_SCK[i]);
    scales[i].set_scale(calibration_factor);
    String key = "off_" + String(i);
    offsetKnown[i] = prefs.isKey(key.c_str()); // Zero is also a valid saved offset.
    if (offsetKnown[i]) scales[i].set_offset(prefs.getLong(key.c_str(), 0));
    else missing |= (1U << i);
    isSensorOk[i] = false;
  }
  loadcellsInitialized = true;
  resetLoadcellBus();
  if (missing) calibrateLoadcells(missing, false);
}

uint8_t tareLoadcells() { return calibrateLoadcells(ALL_LOADCELLS, true); }

bool isLoadcellMeasurementValid(int channel) {
  if (channel < 0 || channel >= 4) return false;
  portENTER_CRITICAL(&loadcellStateMux);
  bool valid = measurementValidLocked(channel, millis());
  portEXIT_CRITICAL(&loadcellStateMux);
  return valid;
}

bool enableNozzleIfSensorValid(int channel) {
  if (channel < 0 || channel >= 4) return false;
  portENTER_CRITICAL(&loadcellStateMux);
  bool valid = measurementValidLocked(channel, millis());
  digitalWrite(Config::SPRAY_PINS[channel], valid ? LOW : HIGH);
  portEXIT_CRITICAL(&loadcellStateMux);
  return valid;
}

void enforceLoadcellSafety() {
  portENTER_CRITICAL(&loadcellStateMux);
  unsigned long now = millis();
  for (int i = 0; i < 4; ++i) {
    if (!measurementValidLocked(i, now)) {
      // An operating channel remains blocked until a successful tare or reboot.
      if (!loadcellCalibrating && activeNozzles[i]) sensorFaultLatched[i] = true;
      isSensorOk[i] = false;
      digitalWrite(Config::SPRAY_PINS[i], HIGH);
    }
  }
  portEXIT_CRITICAL(&loadcellStateMux);
}

void monitorWeight() {
  if (!loadcellsInitialized) return;
  int32_t raw[4];
  uint8_t valid = readLoadcellFrame(raw);
  for (int i = 0; i < 4; ++i) {
    if (!(valid & (1U << i))) continue;
    float scale = scales[i].get_scale();
    float value = ((double)raw[i] - scales[i].get_offset()) / scale;
    if (!isfinite(scale) || scale == 0 || !isfinite(value) || value > 5000 || value < -500) {
      latchSensorFault(i);
      continue;
    }
    portENTER_CRITICAL(&loadcellStateMux);
    bool usable = offsetKnown[i] && !sensorFaultLatched[i] && !loadcellCalibrating;
    if (usable) {
      sampleSeen[i] = true;
      lastSampleMillis[i] = millis();
      isSensorOk[i] = true;
    }
    portEXIT_CRITICAL(&loadcellStateMux);
    if (usable) {
      weights[i] = firstWeight[i] ? value : value * 0.15f + weights[i] * 0.85f;
      firstWeight[i] = false;
      if (fabsf(weights[i]) < 0.5f) weights[i] = 0;
    }
  }
  enforceLoadcellSafety();
  for (int i = 0; i < 4; ++i) {
    if (!isLoadcellMeasurementValid(i)) { weights[i] = 0; firstWeight[i] = true; }
  }
  static unsigned long lastUiUpdate = 0;
  if (millis() - lastUiUpdate >= 250UL) {
    lastUiUpdate = millis();
    updateScentProgressBars();
  }
}

void resetWeightFilters() {
  portENTER_CRITICAL(&loadcellStateMux);
  for (int i = 0; i < 4; ++i) { sampleSeen[i] = false; isSensorOk[i] = false; }
  portEXIT_CRITICAL(&loadcellStateMux);
  for (int i = 0; i < 4; ++i) { weights[i] = 0; firstWeight[i] = true; }
}

void printCalibrationInfo() {
  // Diagnostics use the same cached frame; never advance the shared clock here.
  Serial.printf("Calibration %.1f | W1:%.1f W2:%.1f W3:%.1f W4:%.1f (g)\r\n",
                calibration_factor, weights[0], weights[1], weights[2], weights[3]);
}
// ===================================================================================
// [5] 오디오 (마이크 수음, 스피커 및 앰비언트 분석)
// ===================================================================================

// 📄 [Hardware.cpp] playSound 함수 전체 교체
void playSound(int trackNum) {
  // 곡 번호가 0 이하일 때만 1번으로 보정하고 상한선 제한은 제거
  int safeTrack = (trackNum > 0) ? trackNum : 1;
  int safeVolume = constrain(currentVolume, 0, 30);

  // --- [오버랩 방지 로직] ---
  static int lastPlayedTrack = -1;
  static unsigned long lastPlayTime = 0;
  if (safeTrack == lastPlayedTrack && millis() - lastPlayTime < 3500) {
      Serial.printf(C_YELLOW "[Audio] 중복 재생 방지: 이미 %d번 트랙이 재생/로딩 중입니다.\r\n" C_RESET, safeTrack);
      return; 
  }
  lastPlayedTrack = safeTrack;
  lastPlayTime = millis();
  // --------------------------

  Serial.printf(C_GREEN "[Audio] play request: track=%d volume=%d busy=%d\r\n" C_RESET,
                safeTrack, safeVolume, digitalRead(PIN_BUSY));

  myDFPlayer.stop();
  delay(120);
  myDFPlayer.volume(safeVolume);
  delay(30);
  
  // ★ MP3 폴더 지정 재생(playMp3Folder)으로 변경하여 26번(0026.MP3)을 정확히 재생
  myDFPlayer.playMp3Folder(safeTrack);
}

void initMicrophone() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void runSoundVisualizer() {
  if (currentMode != MODE_AMBIENT) return;
  int32_t sample = 0; size_t bytes_read = 0;
  i2s_read(I2S_PORT, &sample, sizeof(sample), &bytes_read, 0);
  if (bytes_read == 0) return;
  
  unsigned long currentTime = millis();
  static unsigned long lastVisUpdate = 0;
  if (currentTime - lastVisUpdate > 100) { 
    lastVisUpdate = currentTime;
    int vol = abs(sample >> 14); 
    if (vol < 100) vol = 0;
    int bars = constrain(map(vol, 0, 5000, 0, 30), 0, 30); 
    Serial.print("\r\033[K"); Serial.print(C_CYAN "🎤 Sound: " C_RESET C_GREEN);
    for(int i = 0; i < bars; i++) Serial.print("█");
    Serial.printf(C_RESET " [%d]", vol);
  }
}

void runSoundReaction() {
  if (currentMode != MODE_AMBIENT) return;
  int32_t samples[64]; size_t bytes_read = 0;
  i2s_read(I2S_PORT, &samples, sizeof(samples), &bytes_read, 0);
  if (bytes_read == 0) return;

  int db = calculateDecibels(samples, bytes_read / sizeof(int32_t));
  unsigned long currentTime = millis();
  
  if (db >= soundThreshold && (currentTime - lastReactionTime > 5000)) { 
    lastReactionTime = currentTime;
    triggerSpray(random(1, 5), 3, 0, "LOUD NOISE TRIGGER", false);
  }
}

void checkBackgroundNoise() {
  if (currentMode != MODE_AMBIENT) return;
  int32_t samples[32]; size_t bytes_read = 0;
  i2s_read(I2S_PORT, &samples, sizeof(samples), &bytes_read, 0);
  if (bytes_read == 0) return;
  
  int current_db = calculateDecibels(samples, bytes_read / sizeof(int32_t));
  currentDbLevel = current_db; 
  
  static float smoothed_db = 0;
  smoothed_db = (0.3 * current_db) + (0.7 * smoothed_db);
  
}

void sensorTaskLoop(void *parameter) {
  for (;;) {
    enforceLoadcellSafety();
    if (currentMode != MODE_REACTIVE && currentMode != MODE_DASHBOARD && currentMode != MODE_VISUAL) {
        checkBackgroundNoise();
    }
    vTaskDelay(2 / portTICK_PERIOD_MS); 
  }
}

// --- [1시간 슬라이딩 윈도우 헬퍼 함수] ---
void addDbToSlidingWindow(float db) {
  dbHistory[dbHistoryIndex] = db;
  dbHistoryIndex = (dbHistoryIndex + 1) % Config::HOURLY_BUFFER_SIZE;
  if (dbHistoryCount < Config::HOURLY_BUFFER_SIZE) dbHistoryCount++;
}

float get1HourDbAverage() {
  if (dbHistoryCount == 0) return 0.0f;
  float sum = 0;
  for (int i = 0; i < dbHistoryCount; i++) {
    sum += dbHistory[i];
  }
  return sum / dbHistoryCount;
}

void resetSlidingWindow() {
  dbHistoryCount = 0;
  dbHistoryIndex = 0;
  Serial.println(C_YELLOW "🔄 [1시간 슬라이딩 윈도우] 스파이크 감지로 버퍼가 리셋되었습니다." C_RESET);
}

void runAmbientMode() {
  if (currentMode != MODE_AMBIENT || isWaitingForTestDb) return;

  unsigned long currentTime = millis();
  
  bool isSongFinished = (digitalRead(PIN_BUSY) == HIGH && (currentTime - lastAmbientTime > 3000)); // Config:: 삭제
  bool isOneHourPassed = (last1HourCheckTime > 0 && (currentTime - last1HourCheckTime >= Config::ONE_HOUR_MS));

  // -----------------------------------------------------------------------------------
  // [트리거 조건]: 1) 최초 실행  2) 1시간 경과  3) 노래 종료  4) 강제 스킵(n)
  // -----------------------------------------------------------------------------------
  if (isFirstAmbientRun || isOneHourPassed || isSongFinished || forceAmbientSkip) {
    forceAmbientSkip = false;
    
    if (isFirstAmbientRun || isOneHourPassed) {
      last1HourCheckTime = currentTime;
    }

    // [단계 1] 무음 구간(Silence Gap) 수음: BGM을 잠시 끄고 3초간 순수 앰비언트 소음 측정
    myDFPlayer.stop(); 
    delay(100);

    Serial.printf(C_CYAN "\r\n[Silence Gap] 🤫 BGM 무음 구간: 3초간 앰비언트 소음 측정 중...\r\n" C_RESET);

    const int sampleCount = 100;
    int dbSamples[sampleCount];
    long sumDB = 0;
    
    for (int i = 0; i < sampleCount; i++) {
      esp_task_wdt_reset();
      checkNextionInput();
      checkSerialInput();
      monitorWeight();
      runSprayLogic();
      // A stop, mode change or safety trip cancels this measurement cycle.
      if (currentMode != MODE_AMBIENT) return;

      int32_t samples[64]; 
      size_t bytes_read = 0;
      i2s_read(I2S_PORT, &samples, sizeof(samples), &bytes_read, I2S_READ_TIMEOUT);
      
      int current_db = (bytes_read > 0) ? calculateDecibels(samples, bytes_read / sizeof(int32_t)) : 0;
      dbSamples[i] = current_db; 
      sumDB += current_db;

      updateClockDisplay();
      delay(30); // 30ms * 100 = 3초 수음
    }

    float avgDB = (float)sumDB / sampleCount;
    float stdDev = calculateStdDev(dbSamples, sampleCount, avgDB);

    float previous1HourAvg = get1HourDbAverage();
    bool isSpike = false;

    if (dbHistoryCount > 0) {
      if (abs(avgDB - previous1HourAvg) >= 15.0f || avgDB >= 75.0f) {
        isSpike = true;
        Serial.printf(C_RED "🚨 [Fast-Track] 소음 스파이크 감지! (현재: %.1fdB, 1시간평균: %.1fdB)\r\n" C_RESET, avgDB, previous1HourAvg);
        resetSlidingWindow();
        last1HourCheckTime = currentTime;
      }
    }

    addDbToSlidingWindow(avgDB);
    float current1HourAvg = get1HourDbAverage();

    Serial.printf(C_GREEN "📊 [소음 분석] 현재: %.1f dB | 1시간 average: %.1f dB | Spike: %s\r\n" C_RESET, 
                  avgDB, current1HourAvg, isSpike ? "YES" : "NO");

    String payload = "{\"mode\": \"ambient\","
                     "\"db_avg\": " + String(avgDB, 1) + ","
                     "\"db_1hr_avg\": " + String(current1HourAvg, 1) + ","
                     "\"is_spike\": " + String(isSpike ? "true" : "false") + ","
                     "\"db_stddev\": " + String(stdDev, 1) + ","
                     "\"weights\": [" + String(weights[0], 1) + ", " + String(weights[1], 1) + 
                     ", " + String(weights[2], 1) + ", " + String(weights[3], 1) + "]}";
    
    sendServerRequest(payload);
    isFirstAmbientRun = false;

    // [단계 3] 다음 BGM 트랙 연속 재생
    currentAmbientTrack++;
    if (currentAmbientTrack > 20) currentAmbientTrack = 1;
    playSound(currentAmbientTrack);
    
    lastAmbientTime = millis();
  }

  // 실시간 백그라운드 스파이크 감시
  static unsigned long lastSpikeCheck = 0;
  if (currentTime - lastSpikeCheck > 1000) {
    lastSpikeCheck = currentTime;
    float currentAvg = get1HourDbAverage();
    if (currentAvg > 0 && (abs(currentDbLevel - currentAvg) >= 15.0f || currentDbLevel >= 75.0f)) {
      Serial.printf(C_RED "⚡ [Instant Spike] BGM 재생 중 돌발 소음 감지(%d dB)! 즉시 재수음 진입.\r\n" C_RESET, currentDbLevel);
      forceAmbientSkip = true;
    }
  }
}
