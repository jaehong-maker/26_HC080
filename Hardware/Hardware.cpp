#include "Globals.h"

// ===================================================================================
// [1] 유틸리티 (수학 계산 및 정렬 알고리즘)
// ===================================================================================

static int calculateDecibels(const int16_t* samples, int numSamples) {
  if (numSamples <= 0) return 0;
  // Relative PCM level, not calibrated dB SPL. Remove DC before computing RMS.
  float mean = 0.0f;
  for (int i = 0; i < numSamples; i++) mean += samples[i];
  mean /= numSamples;
  float sumSquares = 0.0f;
  for (int i = 0; i < numSamples; i++) {
    float wave = samples[i] - mean;
    sumSquares += wave * wave;
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

namespace {
const char *ledOutputStage = "startup";
}
static void recordLedOutput(uint8_t r, uint8_t g, uint8_t b, const char *source);

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
  recordLedOutput(r, g, b, ledOutputStage);
}

void bootAnimation() {
  ledOutputStage = "boot-animation";
  for (int i = 0; i < 3; i++) {
    setLedColor(ledR, ledG, ledB); 
    delay(100);
    setLedColor(0, 0, 0); 
    delay(100);
  }
  ledOutputStage = "runtime";
}

namespace {
enum class LedWarning { None, Sensor, Wifi, LowFluid };
const char *ledStatus = "startup";
constexpr unsigned long SENSOR_LED_DELAY_MS = 1000;
constexpr unsigned long WIFI_LED_DELAY_MS = 3000;
struct LedFaultDebounce {
  bool active = false;
  bool ready = false;
  unsigned long since = 0;
  unsigned long clearedAt = 0;
  uint32_t occurrences = 0;
  constexpr bool update(bool fault, unsigned long now, unsigned long delayMs) {
    if (!fault) {
      if (active) clearedAt = now;
      active = false;
      ready = false;
      return false;
    }
    if (!active) {
      active = true;
      since = now;
      ++occurrences;
    }
    ready = now - since >= delayMs;
    return ready;
  }
};
LedFaultDebounce sensorLedFault;
LedFaultDebounce wifiLedFault;
uint8_t ledSensorInvalidMask = 0;
bool ledAudioFault = false;
constexpr size_t LED_OUTPUT_HISTORY_SIZE = 16;
struct LedOutputEvent {
  unsigned long atMillis;
  uint8_t r, g, b;
  int brightness;
  int mode;
  const char *status;
  const char *source;
};
LedOutputEvent ledOutputHistory[LED_OUTPUT_HISTORY_SIZE] = {};
size_t ledOutputWriteIndex = 0;
size_t ledOutputHistoryCount = 0;
uint32_t ledOutputChangeCount = 0;
uint32_t ledOutputShowCount = 0;
}
const char *getLedStatus() { return ledStatus; }

static void recordLedOutput(uint8_t r, uint8_t g, uint8_t b, const char *source) {
  // Main-loop/boot owned. Record commands sent to the strip, not optical
  // feedback; unchanged redraws are counted without flooding the history.
  ++ledOutputShowCount;
  if (ledOutputHistoryCount) {
    const LedOutputEvent &last = ledOutputHistory[(ledOutputWriteIndex + LED_OUTPUT_HISTORY_SIZE - 1) % LED_OUTPUT_HISTORY_SIZE];
    if (last.r == r && last.g == g && last.b == b && last.brightness == ledBrightness &&
        strcmp(last.status, ledStatus) == 0 && strcmp(last.source, source) == 0) return;
  }
  ledOutputHistory[ledOutputWriteIndex] = {millis(), r, g, b, ledBrightness,
      static_cast<int>(currentMode), ledStatus, source};
  ledOutputWriteIndex = (ledOutputWriteIndex + 1) % LED_OUTPUT_HISTORY_SIZE;
  if (ledOutputHistoryCount < LED_OUTPUT_HISTORY_SIZE) ++ledOutputHistoryCount;
  ++ledOutputChangeCount;
}

void systemHeartbeat() {
  const unsigned long currentTime = millis();
  static unsigned long lastUpdate = 0;
  static uint16_t hue = 0;

  bool isAnySensorBroken = !isAudioOk;
  bool isFluidLow = false;
  ledAudioFault = !isAudioOk;
  ledSensorInvalidMask = 0;
  for (int i = 0; i < 4; i++) {
    const bool valid = isLoadcellMeasurementValid(i);
    if (!valid) {
      isAnySensorBroken = true;
      ledSensorInvalidMask |= (1U << i);
    }
    // Match the existing pump cutoff, including a valid zero/empty reading.
    else if (calculateScentPercent(weights[i]) < Config::MIN_SCENT_PERCENT) isFluidLow = true;
  }
  // Presentation debounce only. Sensor validity and immediate relay cutoff
  // remain in the independent loadcell safety path, without this delay.
  const bool sensorWarningReady = sensorLedFault.update(isAnySensorBroken, currentTime, SENSOR_LED_DELAY_MS);
  const bool wifiWarningReady = wifiLedFault.update(WiFi.status() != WL_CONNECTED, currentTime, WIFI_LED_DELAY_MS);
  if (!ledEnabled) {
    ledStatus = "disabled";
    setLedColor(0, 0, 0);
    return;
  }

  // All runtime LED writes use this priority, including server/local updates.
  // Keep sensor and Wi-Fi warnings ahead of the refill indication.
  LedWarning warning = sensorWarningReady ? LedWarning::Sensor :
      wifiWarningReady ? LedWarning::Wifi :
      isFluidLow ? LedWarning::LowFluid : LedWarning::None;
  static LedWarning previousWarning = LedWarning::None;
  static unsigned long warningStartedAt = 0;
  if (warning != previousWarning) {
    previousWarning = warning;
    warningStartedAt = currentTime;
    // A recovered warning must also repaint immediately in rainbow mode.
    if (warning == LedWarning::None) lastUpdate = currentTime - 20UL;
  }
  if (warning != LedWarning::None) {
    const unsigned long interval = warning == LedWarning::Sensor ? 300UL :
        warning == LedWarning::Wifi ? 600UL : 500UL;
    const bool on = ((currentTime - warningStartedAt) / interval) % 2 == 0;
    const bool blue = warning == LedWarning::Wifi;
    ledStatus = warning == LedWarning::Sensor ? "sensor-or-audio-fault" :
        blue ? "wifi-disconnected" : "low-fluid";
    setLedColor(on && !blue ? 255 : 0, 0, on && blue ? 255 : 0);
    return;
  }
  ledStatus = "normal";

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
          uint32_t color = strip.getPixelColor(4);
          recordLedOutput((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff, "rainbow");
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

void appendLedDiagnostics(JsonDocument &doc) {
  const unsigned long now = millis();
  doc["led_effect"] = static_cast<int>(ledEffect);
  doc["led_output_trace_kind"] = "commanded-rgb-not-optical-feedback";
  doc["led_output_show_count"] = ledOutputShowCount;
  doc["led_output_change_count"] = ledOutputChangeCount;
  JsonArray output = doc["led_output_events"].to<JsonArray>();
  const size_t oldest = (ledOutputWriteIndex + LED_OUTPUT_HISTORY_SIZE - ledOutputHistoryCount) % LED_OUTPUT_HISTORY_SIZE;
  for (size_t i = 0; i < ledOutputHistoryCount; ++i) {
    const LedOutputEvent &event = ledOutputHistory[(oldest + i) % LED_OUTPUT_HISTORY_SIZE];
    JsonObject entry = output.add<JsonObject>();
    entry["at_ms"] = event.atMillis;
    JsonArray color = entry["rgb"].to<JsonArray>();
    color.add(event.r); color.add(event.g); color.add(event.b);
    entry["brightness"] = event.brightness;
    entry["mode"] = event.mode;
    entry["status"] = event.status;
    entry["source"] = event.source;
  }
  JsonObject debounce = doc["led_warning_debounce"].to<JsonObject>();
  debounce["sensor_invalid_mask"] = ledSensorInvalidMask;
  debounce["audio_fault"] = ledAudioFault;
  auto addState = [&](const char *name, const LedFaultDebounce &state, unsigned long delayMs) {
    JsonObject entry = debounce[name].to<JsonObject>();
    entry["delay_ms"] = delayMs;
    entry["fault_active"] = state.active;
    entry["fault_elapsed_ms"] = state.active ? now - state.since : 0;
    entry["warning_pending"] = state.active && !state.ready;
    entry["warning_ready"] = state.ready;
    entry["occurrences"] = state.occurrences;
    entry["last_started_at_ms"] = state.since;
    entry["last_cleared_at_ms"] = state.clearedAt;
  };
  addState("sensor_or_audio", sensorLedFault, SENSOR_LED_DELAY_MS);
  addState("wifi", wifiLedFault, WIFI_LED_DELAY_MS);
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
  
  recordControlDiagnostic("stop", "outputs-off");
  stopAudioPlayback();
  currentAmbientTrack = 0;
}

void resetManualState() {
  forceAllOff();
  stopAudioPlayback();
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
// HX711 settling after reset is typically 400 ms at 10 SPS. No clocks are
// issued during this guard; the shared 25-pulse reader remains unchanged.
constexpr unsigned long RESET_SETTLE_MS = 500;
constexpr unsigned long BOOT_ATTEMPT_MS = 1500;
constexpr int BOOT_MAX_ATTEMPTS = 3;
constexpr int BOOT_VALID_FRAMES = 2;
portMUX_TYPE loadcellStateMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE loadcellClockMux = portMUX_INITIALIZER_UNLOCKED;
bool loadcellsInitialized = false;
bool loadcellCalibrating = false;
bool loadcellPriming = false;
unsigned long busResetAt = 0;
uint8_t bootAttempts = 0;
uint8_t bootFaultMask = 0;
const char *sensorFaultReason[4] = {"none", "none", "none", "none"};
unsigned long sensorFaultAt[4] = {};
int32_t lastRawFrame[4] = {};
uint32_t loadcellFrameSequence = 0;
bool offsetKnown[4] = {};
bool sampleSeen[4] = {};
bool sensorFaultLatched[4] = {};
bool firstWeight[4] = {true, true, true, true};
unsigned long lastSampleMillis[4] = {};
bool waitingForReady = false;
unsigned long readyWaitStart = 0;

bool measurementValidLocked(int i, unsigned long now) {
  return loadcellsInitialized && !loadcellCalibrating && !loadcellPriming && offsetKnown[i] &&
         !sensorFaultLatched[i] && sampleSeen[i] && isSensorOk[i] &&
         now - lastSampleMillis[i] <= SAMPLE_MAX_AGE_MS;
}

void latchSensorFault(int i, const char *reason) {
  portENTER_CRITICAL(&loadcellStateMux);
  sensorFaultLatched[i] = true;
  sensorFaultReason[i] = reason;
  sensorFaultAt[i] = millis();
  if (loadcellPriming) bootFaultMask |= (1U << i);
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
  busResetAt = millis();
}

uint8_t readLoadcellFrame(int32_t raw[4]) {
  for (int i = 0; i < 4; ++i) raw[i] = 0;
  if (millis() - busResetAt < RESET_SETTLE_MS) return 0;
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
    if (!(valid & (1U << i))) latchSensorFault(i, "frame-desync");
    lastRawFrame[i] = raw[i];
  }
  ++loadcellFrameSequence;
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
    sensorFaultReason[i] = ok ? "none" : "tare-failed";
    portEXIT_CRITICAL(&loadcellStateMux);
    Serial.printf("[LoadCell %d] tare %s\r\n", i + 1, ok ? "OK" : "TIMEOUT / INVALID");
  }
  resetWeightFilters();
  portENTER_CRITICAL(&loadcellStateMux);
  loadcellCalibrating = false;
  portEXIT_CRITICAL(&loadcellStateMux);
  return success;
}
// Boot-only recovery, with outputs already forced off. It validates ADC
// frames against the saved offsets; it never tares a loaded cartridge.
void primeLoadcellsAtBoot(uint8_t required) {
  loadcellPriming = true;
  uint8_t stable = 0;
  float values[4] = {};
  unsigned long sampledAt[4] = {};
  for (int attempt = 0; attempt < BOOT_MAX_ATTEMPTS; ++attempt) {
    ++bootAttempts;
    uint8_t consecutive[4] = {};
    stable = 0;
    resetLoadcellBus();
    const unsigned long started = millis();
    while (millis() - started < BOOT_ATTEMPT_MS) {
      int32_t raw[4];
      const uint32_t previousFrame = loadcellFrameSequence;
      const uint8_t valid = readLoadcellFrame(raw);
      if (loadcellFrameSequence != previousFrame) {
        for (int i = 0; i < 4; ++i) {
          const float scale = scales[i].get_scale();
          const float value = ((double)raw[i] - scales[i].get_offset()) / scale;
          const bool acceptable = (valid & required & (1U << i)) &&
              isfinite(scale) && scale != 0 && isfinite(value) && value <= 5000 && value >= -500;
          if (acceptable) {
            if (consecutive[i] < BOOT_VALID_FRAMES) ++consecutive[i];
            values[i] = value;
            sampledAt[i] = millis();
          } else {
            consecutive[i] = 0;
            if ((valid & required & (1U << i)) && (!isfinite(value) || value > 5000 || value < -500))
              latchSensorFault(i, "out-of-range");
          }
        }
        stable = 0;
        for (int i = 0; i < 4; ++i) if (consecutive[i] >= BOOT_VALID_FRAMES) stable |= (1U << i);
        if ((stable & required) == required) break;
      }
      esp_task_wdt_reset();
      delay(2);
    }
    if ((stable & required) == required) break;
  }
  portENTER_CRITICAL(&loadcellStateMux);
  for (int i = 0; i < 4; ++i) {
    const bool ok = (stable & required & (1U << i)) != 0;
    sensorFaultLatched[i] = !ok;
    sensorFaultReason[i] = ok ? "none" : !offsetKnown[i] ? "missing-offset" :
        strcmp(sensorFaultReason[i], "none") == 0 ? "startup-not-stable" : sensorFaultReason[i];
    sampleSeen[i] = ok;
    isSensorOk[i] = ok;
    lastSampleMillis[i] = sampledAt[i];
  }
  loadcellPriming = false;
  portEXIT_CRITICAL(&loadcellStateMux);
  for (int i = 0; i < 4; ++i) {
    const bool ok = (stable & required & (1U << i)) != 0;
    weights[i] = ok ? values[i] : 0.0f;
    firstWeight[i] = !ok;
  }
}
} // namespace

void initializeLoadcells() {
  uint8_t known = 0;
  for (int i = 0; i < 4; ++i) {
    scales[i].begin(LOADCELL_DT[i], LOADCELL_SCK[i]);
    scales[i].set_scale(calibration_factor);
    String key = "off_" + String(i);
    offsetKnown[i] = prefs.isKey(key.c_str()); // Zero is also a valid saved offset.
    if (offsetKnown[i]) {
      scales[i].set_offset(prefs.getLong(key.c_str(), 0));
      known |= (1U << i);
    } else {
      sensorFaultReason[i] = "missing-offset";
    }
    isSensorOk[i] = false;
  }
  loadcellsInitialized = true;
  // Missing calibration stays blocked until an explicit empty-platform tare.
  // Boot never overwrites saved offsets or treats a loaded platform as zero.
  if (known) primeLoadcellsAtBoot(known);
  else resetLoadcellBus();
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
      if (!loadcellCalibrating && !loadcellPriming && activeNozzles[i] && !sensorFaultLatched[i]) {
        sensorFaultLatched[i] = true;
        sensorFaultReason[i] = "stale-active";
        sensorFaultAt[i] = now;
      }
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
      latchSensorFault(i, "out-of-range");
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


void appendLoadcellDiagnostics(JsonDocument &doc) {
  struct Snapshot { bool known, seen, latched, valid; unsigned long age, faultAt; const char *reason; };
  Snapshot state[4];
  const unsigned long now = millis();
  portENTER_CRITICAL(&loadcellStateMux);
  for (int i = 0; i < 4; ++i) {
    state[i] = {offsetKnown[i], sampleSeen[i], sensorFaultLatched[i], measurementValidLocked(i, now),
        now - lastSampleMillis[i], sensorFaultAt[i], sensorFaultReason[i]};
  }
  portEXIT_CRITICAL(&loadcellStateMux);
  doc["loadcell_boot_attempts"] = bootAttempts;
  doc["loadcell_boot_fault_mask"] = bootFaultMask;
  JsonArray channels = doc["loadcells"].to<JsonArray>();
  for (int i = 0; i < 4; ++i) {
    JsonObject entry = channels.add<JsonObject>();
    entry["channel"] = i + 1;
    entry["offset_known"] = state[i].known;
    entry["offset"] = scales[i].get_offset();
    entry["raw"] = lastRawFrame[i];
    entry["sample_seen"] = state[i].seen;
    if (state[i].seen) entry["sample_age_ms"] = state[i].age;
    else entry["sample_age_ms"] = nullptr;
    entry["fault_latched"] = state[i].latched;
    entry["valid"] = state[i].valid;
    entry["reason"] = !state[i].known ? "missing-offset" : state[i].latched ? state[i].reason :
        !state[i].seen ? "awaiting-sample" : state[i].age > SAMPLE_MAX_AGE_MS ? "stale-sample" : "ok";
    entry["fault_at_ms"] = state[i].faultAt;
    entry["weight_g"] = weights[i];
    entry["percent"] = calculateScentPercent(weights[i]);
  }
}

void printCalibrationInfo() {
  // Diagnostics use the same cached frame; never advance the shared clock here.
  Serial.printf("Calibration %.1f | W1:%.1f W2:%.1f W3:%.1f W4:%.1f (g)\r\n",
                calibration_factor, weights[0], weights[1], weights[2], weights[3]);
}
// ===================================================================================
// [5] 오디오 (마이크 수음, 스피커 및 앰비언트 분석)
// ===================================================================================

namespace {
int lastRequestedTrack = -1;
unsigned long lastAudioPlayMillis = 0;
constexpr unsigned long AUDIO_DUPLICATE_WINDOW_MS = 3500;
constexpr unsigned long AUDIO_START_GRACE_MS = 500;
}

void stopAudioPlayback() {
  // Invalidate first: BUSY may still read LOW briefly after an explicit stop.
  lastRequestedTrack = -1;
  lastAudioPlayMillis = 0;
  myDFPlayer.stop();
}

void playSound(int trackNum) {
  // 곡 번호가 0 이하일 때만 1번으로 보정하고 상한선 제한은 제거
  int safeTrack = (trackNum > 0) ? trackNum : 1;
  int safeVolume = constrain(currentVolume, 0, 30);

  const unsigned long sincePlay = millis() - lastAudioPlayMillis;
  const bool playingOrStarting = digitalRead(PIN_BUSY) == LOW || sincePlay < AUDIO_START_GRACE_MS;
  if (safeTrack == lastRequestedTrack && sincePlay < AUDIO_DUPLICATE_WINDOW_MS && playingOrStarting) {
      Serial.printf(C_YELLOW "[Audio] 중복 재생 방지: 이미 %d번 트랙이 재생/로딩 중입니다.\r\n" C_RESET, safeTrack);
      return; 
  }

  Serial.printf(C_GREEN "[Audio] play request: track=%d volume=%d busy=%d\r\n" C_RESET,
                safeTrack, safeVolume, digitalRead(PIN_BUSY));

  stopAudioPlayback();
  delay(120);
  myDFPlayer.volume(safeVolume);
  delay(30);
  
  // ★ MP3 폴더 지정 재생(playMp3Folder)으로 변경하여 26번(0026.MP3)을 정확히 재생
  myDFPlayer.playMp3Folder(safeTrack);
  // Count the loading grace from the actual play command, not the stop delay.
  lastRequestedTrack = safeTrack;
  lastAudioPlayMillis = millis();
}

namespace {
portMUX_TYPE microphoneMux = portMUX_INITIALIZER_UNLOCKED;
bool microphoneReady = false;
MicrophoneLevel microphoneLevel = {};
int16_t *recordingDestination = nullptr;
size_t recordingCapacity = 0;
size_t recordingCount = 0;

static int16_t microphoneWordToPcm(int32_t word) {
  // INMP441: signed 24-bit data in the upper bits of a 32-bit I2S slot.
  return static_cast<int16_t>(word >> 16);
}

void captureMicrophoneFrame() {
  if (!microphoneReady) return;
  int32_t raw[128];
  size_t bytesRead = 0;
  // Sole DMA reader. Never wait here: the same task enforces nozzle safety.
  if (i2s_read(I2S_PORT, raw, sizeof(raw), &bytesRead, 0) != ESP_OK || bytesRead == 0) {
    portENTER_CRITICAL(&microphoneMux);
    if (millis() - microphoneLevel.capturedAtMillis > 250UL) currentDbLevel = 0;
    portEXIT_CRITICAL(&microphoneMux);
    return;
  }
  size_t count = bytesRead / sizeof(raw[0]);
  int16_t pcm[128];
  int peak = 0;
  for (size_t i = 0; i < count; ++i) {
    pcm[i] = microphoneWordToPcm(raw[i]);
    int magnitude = abs(static_cast<int>(pcm[i]));
    if (magnitude > peak) peak = magnitude;
  }
  int level = calculateDecibels(pcm, count);
  portENTER_CRITICAL(&microphoneMux);
  microphoneLevel.level = level;
  microphoneLevel.peak = peak;
  microphoneLevel.capturedAtMillis = millis();
  ++microphoneLevel.sequence;
  currentDbLevel = level;
  if (recordingDestination && recordingCount < recordingCapacity) {
    size_t remaining = recordingCapacity - recordingCount;
    size_t copyCount = count < remaining ? count : remaining;
    memcpy(recordingDestination + recordingCount, pcm, copyCount * sizeof(int16_t));
    recordingCount += copyCount;
  }
  portEXIT_CRITICAL(&microphoneMux);
}
} // namespace

void initMicrophone() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
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
  esp_err_t error = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (error == ESP_OK) {
    error = i2s_set_pin(I2S_PORT, &pin_config);
    if (error != ESP_OK) i2s_driver_uninstall(I2S_PORT);
  }
  microphoneReady = error == ESP_OK;
  if (!microphoneReady) Serial.printf("[Microphone] Initialization failed: %d\r\n", error);
}

bool getMicrophoneLevel(MicrophoneLevel &snapshot) {
  portENTER_CRITICAL(&microphoneMux);
  snapshot = microphoneLevel;
  bool valid = microphoneReady && snapshot.sequence != 0 && millis() - snapshot.capturedAtMillis <= 250UL;
  portEXIT_CRITICAL(&microphoneMux);
  return valid;
}

bool recordMicrophonePcm(int16_t *destination, size_t sampleCount, unsigned long timeoutMs) {
  if (!destination || sampleCount == 0 || !microphoneReady || !SensorTaskHandle) return false;
  portENTER_CRITICAL(&microphoneMux);
  if (recordingDestination) { portEXIT_CRITICAL(&microphoneMux); return false; }
  recordingDestination = destination;
  recordingCapacity = sampleCount;
  recordingCount = 0;
  portEXIT_CRITICAL(&microphoneMux);
  unsigned long started = millis();
  bool complete = false;
  while (millis() - started < timeoutMs && currentMode == MODE_VOICE) {
    portENTER_CRITICAL(&microphoneMux);
    complete = recordingCount == sampleCount;
    portEXIT_CRITICAL(&microphoneMux);
    if (complete) break;
    esp_task_wdt_reset();
    monitorWeight();
    runSprayLogic();
    checkNextionInput();
    processPendingServerResponse();
    handleWebClient();
    delay(5);
  }
  // Detach under the same lock used by the capture task before the caller can
  // upload/free this buffer. No capture callback may retain its old address.
  portENTER_CRITICAL(&microphoneMux);
  complete = recordingCount == sampleCount && currentMode == MODE_VOICE;
  recordingDestination = nullptr;
  recordingCapacity = 0;
  recordingCount = 0;
  portEXIT_CRITICAL(&microphoneMux);
  if (!complete) Serial.println("[Microphone] Recording cancelled or timed out; partial WAV discarded.");
  return complete;
}

void runSoundVisualizer() {
  if (currentMode != MODE_VISUAL) return;
  MicrophoneLevel snapshot;
  if (!getMicrophoneLevel(snapshot)) return;
  static unsigned long lastVisUpdate = 0;
  if (millis() - lastVisUpdate <= 100) return;
  lastVisUpdate = millis();
  int bars = constrain(map(snapshot.peak, 0, 5000, 0, 30), 0, 30);
  Serial.print("\r\033[K"); Serial.print(C_CYAN "Microphone: " C_RESET C_GREEN);
  for (int i = 0; i < bars; ++i) Serial.print("#");
  Serial.printf(C_RESET " [%d]", snapshot.peak);
}

void runSoundReaction() {
  if (currentMode != MODE_REACTIVE) return;
  MicrophoneLevel snapshot;
  if (!getMicrophoneLevel(snapshot)) return;
  if (digitalRead(PIN_BUSY) == LOW) return; // Own BGM cannot request a spray.
  if (snapshot.level >= soundThreshold && millis() - lastReactionTime > 5000) {
    lastReactionTime = millis();
    triggerSpray(random(1, 5), 3, 0, "LOUD NOISE TRIGGER", false);
  }
}

void checkBackgroundNoise() {
  MicrophoneLevel snapshot;
  if (getMicrophoneLevel(snapshot)) currentDbLevel = snapshot.level;
}

void sensorTaskLoop(void *parameter) {
  for (;;) {
    enforceLoadcellSafety();
    captureMicrophoneFrame();
    vTaskDelay(pdMS_TO_TICKS(2) > 0 ? pdMS_TO_TICKS(2) : 1);
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
  if (currentMode != MODE_AMBIENT) return;
  // The capture task is the only microphone reader. POLL reports its current
  // snapshot; the server owns scent selection and music changes.
  checkBackgroundNoise();
}
