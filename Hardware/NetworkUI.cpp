#include "Globals.h"
#include <new>
#include <lwip/sockets.h>
#include <errno.h>
#include <freertos/semphr.h>

static WiFiClientSecure sharedClient;
static bool isSessionInit = false;
static bool automaticOfflineMode = false;

struct PendingServerRequest {
  String body;
  uint32_t stopGeneration;
};

struct PendingServerResponse {
  String body;
  bool weatherRefresh;
  bool weatherRetry;
  uint32_t stopGeneration;
};
static QueueHandle_t serverResponseQueue = nullptr;
static portMUX_TYPE commandOrderMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t stopGeneration = 0;
static int lastSyncedManualScent = 0;
// A matching request/response pair, captured at HTTP transmission rather than
// enqueue time. Never hold this mutex during HTTP or actuator work.
struct PollDiagnostic {
  String request;
  String response;
  unsigned long requestAt = 0;
  unsigned long responseAt = 0;
  uint32_t sequence = 0;
  int httpCode = 0;
  bool inFlight = false;
};
static PollDiagnostic pollDiagnostic;
static SemaphoreHandle_t pollDiagnosticMutex = nullptr;

static void beginPollDiagnostic(const String &payload) {
  if (!pollDiagnosticMutex) return;
  if (xSemaphoreTake(pollDiagnosticMutex, portMAX_DELAY) != pdTRUE) return;
  pollDiagnostic.request = payload;
  pollDiagnostic.response = "";
  pollDiagnostic.requestAt = millis();
  pollDiagnostic.responseAt = 0;
  ++pollDiagnostic.sequence;
  pollDiagnostic.httpCode = 0;
  pollDiagnostic.inFlight = true;
  xSemaphoreGive(pollDiagnosticMutex);
}

static void finishPollDiagnostic(const String &response, int httpCode) {
  if (!pollDiagnosticMutex) return;
  if (xSemaphoreTake(pollDiagnosticMutex, portMAX_DELAY) != pdTRUE) return;
  pollDiagnostic.response = response;
  pollDiagnostic.responseAt = millis();
  pollDiagnostic.httpCode = httpCode;
  pollDiagnostic.inFlight = false;
  xSemaphoreGive(pollDiagnosticMutex);
}

namespace {
constexpr size_t DIAGNOSTIC_EVENT_COUNT = 12;
struct ControlDiagnosticEvent {
  uint32_t atMillis;
  char event[24];
  char detail[64];
  uint8_t gpioMask;
  uint8_t activeMask;
  bool running;
  bool spraying;
  int mode;
};
ControlDiagnosticEvent diagnosticEvents[DIAGNOSTIC_EVENT_COUNT] = {};
portMUX_TYPE diagnosticMux = portMUX_INITIALIZER_UNLOCKED;
size_t diagnosticWriteIndex = 0;
size_t diagnosticCount = 0;
uint32_t nextionStopCount = 0;
char lastNextionCommand[64] = {};

uint8_t currentSprayGpioMask() {
  uint8_t mask = 0;
  for (int i = 0; i < 4; ++i) if (digitalRead(Config::SPRAY_PINS[i]) == HIGH) mask |= (1U << i);
  return mask;
}
} // namespace

void recordControlDiagnostic(const char *event, const String &detail) {
  ControlDiagnosticEvent entry = {};
  entry.atMillis = millis();
  snprintf(entry.event, sizeof(entry.event), "%s", event);
  snprintf(entry.detail, sizeof(entry.detail), "%s", detail.c_str());
  entry.gpioMask = currentSprayGpioMask();
  for (int i = 0; i < 4; ++i) if (activeNozzles[i]) entry.activeMask |= (1U << i);
  entry.running = isRunning;
  entry.spraying = isSpraying;
  entry.mode = currentMode;
  portENTER_CRITICAL(&diagnosticMux);
  diagnosticEvents[diagnosticWriteIndex] = entry;
  diagnosticWriteIndex = (diagnosticWriteIndex + 1) % DIAGNOSTIC_EVENT_COUNT;
  if (diagnosticCount < DIAGNOSTIC_EVENT_COUNT) ++diagnosticCount;
  if (strcmp(event, "nextion-rx") == 0) {
    memcpy(lastNextionCommand, entry.detail, sizeof(lastNextionCommand));
    if (detail == "ST") ++nextionStopCount;
  }
  portEXIT_CRITICAL(&diagnosticMux);
}

static uint32_t currentStopGeneration() {
  portENTER_CRITICAL(&commandOrderMux);
  uint32_t generation = stopGeneration;
  portEXIT_CRITICAL(&commandOrderMux);
  return generation;
}

void invalidatePendingServerStarts() {
  portENTER_CRITICAL(&commandOrderMux);
  ++stopGeneration;
  portEXIT_CRITICAL(&commandOrderMux);
}

bool initializeServerResponseQueue() {
  if (!pollDiagnosticMutex) pollDiagnosticMutex = xSemaphoreCreateMutex();
  if (!networkQueue) networkQueue = xQueueCreate(10, sizeof(PendingServerRequest*));
  if (!serverResponseQueue) serverResponseQueue = xQueueCreate(4, sizeof(PendingServerResponse*));
  return networkQueue != nullptr && serverResponseQueue != nullptr;
}

static void queueServerResponse(const String &body, bool weatherRefresh, uint32_t requestGeneration, bool weatherRetry = false) {
  if (!serverResponseQueue) return;
  PendingServerResponse *message = new (std::nothrow) PendingServerResponse{body, weatherRefresh, weatherRetry, requestGeneration};
  if (!message || message->body.length() != body.length()) {
    delete message;
    Serial.println("[Network] Unable to allocate server response; response discarded.");
    return;
  }
  // Backpressure preserves command order, including explicit app stop commands.
  // Waiting here never holds a relay/audio lock or blocks the main control loop.
  if (xQueueSend(serverResponseQueue, &message, portMAX_DELAY) != pdTRUE) delete message;
}

static unsigned long lastLocalSettingsMillis = 0;
static const unsigned long LOCAL_SETTINGS_PROTECT_MS = 30000;
static unsigned long lastLocalStopMillis = 0;
static const unsigned long WEATHER_RESPONSE_IGNORE_AFTER_STOP_MS = 15000;
static unsigned long lastLocalWeatherStartMillis = 0;
static const unsigned long WEATHER_STOP_IGNORE_AFTER_START_MS = 15000;

void markLocalSettingsChanged() { lastLocalSettingsMillis = millis(); }
void markLocalStop() {
  lastLocalStopMillis = millis();
  invalidatePendingServerStarts();
}
void clearLocalStopMark() { lastLocalStopMillis = 0; }
void markLocalWeatherStart() { lastLocalWeatherStartMillis = millis(); }

static bool canApplyServerSettings() {
  return lastLocalSettingsMillis == 0 || (millis() - lastLocalSettingsMillis >= LOCAL_SETTINGS_PROTECT_MS);
}
static bool wasRecentlyStoppedLocally() {
  return lastLocalStopMillis > 0 && millis() - lastLocalStopMillis < WEATHER_RESPONSE_IGNORE_AFTER_STOP_MS;
}

static bool wasWeatherStartedLocallyRecently() {
  return lastLocalWeatherStartMillis > 0 && millis() - lastLocalWeatherStartMillis < WEATHER_STOP_IGNORE_AFTER_START_MS;
}

static bool isServerStopCommand(int cmd, const String &resultText, const String &serverActiveMode) {
  if (serverActiveMode == "ready" || serverActiveMode == "off") return true;
  if (cmd == 90) return true;
  if (resultText == "STOP" || resultText == "OFF") return true;
  if (resultText.indexOf("중지") >= 0 || resultText.indexOf("정지") >= 0) return true;
  if (resultText.indexOf("꺼짐") >= 0 || resultText.indexOf("대기") >= 0) return true;
  return false; 
}

static bool isExplicitServerStop(int cmd, const String &text, const String &mode, bool isAppCmd) {
  return cmd == 0 || cmd == 90 || isServerStopCommand(cmd, text, "") ||
         (isAppCmd && (mode == "ready" || mode == "off"));
}

static bool isExplicitServerStopResponse(const String &body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  int cmd = doc["spray"] | -1;
  String text = doc["result_text"] | "";
  String mode = doc["active_mode"] | "";
  mode.trim();
  bool isAppCmd = (doc["app_command"] == true || doc["pending_cmd"] == true || doc["command_source"] == "app");
  return isExplicitServerStop(cmd, text, mode, isAppCmd);
}

static void applyServerStop(bool explicitStop = true) {
  SystemMode modeBeforeStop = currentMode;
  if (explicitStop || currentMode != MODE_READY || isRunning || isSpraying) markLocalStop();
  else lastLocalStopMillis = millis(); // An idle status is not a new stop barrier.
  clearWeatherState();
  setSystemMode(MODE_READY, "Stopped by Server");
  // LED power/color follows its own settings, including while ready/off.
  if (modeBeforeStop == MODE_WEATHER || currentDisplayPage == PAGE_WEATHER) showWeatherOffPage();
  else updateDisplay(0, "Stopped by Server");
  lastSyncedManualScent = 0;
  manualModeOffMillis = 0;
  lastStoppedManualScent = 0;
  Serial.println("[Server Sync] Stop applied.");
}

static bool readJsonFloatField(JsonDocument &doc, const char *fieldName, float &value) {
  if (doc[fieldName].isNull()) return false;
  String rawValue = doc[fieldName].as<String>(); rawValue.trim();
  if (rawValue.length() == 0) return false;
  value = rawValue.toFloat(); return true;
}

static String attachDeviceIdIfMissing(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) || !doc.is<JsonObject>()) return payload;
  if (doc["action"] == "POLL") doc["deviceId"] = "SS";
  else if (doc["deviceId"].isNull()) doc["deviceId"] = deviceId;
  String normalized; normalized.reserve(payload.length() + 32);
  serializeJson(doc, normalized); return normalized;
}

void rememberWeatherRegion(const String &region) {
  if (region.length() == 0 || region == lastWeatherRegion) return;
  lastWeatherRegion = region;
  if (currentMode == MODE_WEATHER) { hasWeatherSnapshot = false; hasTempHumiSnapshot = false; }
  prefs.putString("last_region", lastWeatherRegion);
  Serial.printf(C_CYAN "\r\n[Weather] Saved region updated: %s\r\n" C_RESET, lastWeatherRegion.c_str());
}

String buildWeatherRequestPayload(const String &region) {
  JsonDocument reqDoc; reqDoc["mode"] = "weather";
  String safeRegion = region; safeRegion.trim();
  if (safeRegion.length() > 0) reqDoc["region"] = safeRegion;
  JsonArray wArray = reqDoc["weights"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    float percent = calculateScentPercent(weights[i]); 
    wArray.add(round(percent * 10.0f) / 10.0f);
  }
  String payload; payload.reserve(128); serializeJson(reqDoc, payload); return payload;
}

void requestWeatherRefresh(const String &region) {
  lastWeatherCallMillis = millis();
  sendServerRequest(buildWeatherRequestPayload(region));
}

static bool isWeatherRequestPayload(const String &payload) { return payload.indexOf("\"mode\":\"weather\"") >= 0; }
static bool shouldProcessNetworkResponse(const String &payload) { return payload.indexOf("\"POLL\"") >= 0 || isWeatherRequestPayload(payload); }
static void allowWeatherRetry() { lastWeatherCallMillis = millis() - WEATHER_INTERVAL; }
static bool isWeatherLikeResponse(const String &serverActiveMode, const String &weatherText, const String &targetRegion, bool isWeatherRefreshResponse) {
  return isWeatherRefreshResponse || serverActiveMode == "weather" || weatherText.length() > 0 || targetRegion.length() > 0;
}

void initNetworkSession() { if (!isSessionInit) { sharedClient.setInsecure(); isSessionInit = true; } }

void displayWatcherTask(void *pvParameters) {
  for(;;) {
    if (nexSerial.available()) {
      char c = (char)nexSerial.read(); static String tempBuf = "";
      String cmd = processNextionChar(c, tempBuf, false);
      if (cmd.length() > 0) {
        if (cmd == "OF") {
          Serial.println(C_GREEN "\r\n🔌 [Offline] 오프라인 버튼 터치 감지!" C_RESET);
          prefs.putBool("force_offline", true); prefs.remove("pending_nextion_cmd");
        } else if (cmd == "RE") {
          Serial.println(C_GREEN "\r\n[WiFi] RE 버튼 감지! Wi-Fi 포털 시작..." C_RESET);
          prefs.putInt("wifi_return_page", prefs.getInt("wifi_return_page", currentDisplayPage));
          prefs.putBool("force_config_portal", true); prefs.putBool("force_offline", false);
          prefs.remove("pending_nextion_cmd"); showPage(PAGE_WIFI_RESET);
          WiFiManager wifiManager; wifiManager.resetSettings(); WiFi.disconnect(true, true);
        } else {
          prefs.putBool("force_offline", true); prefs.putString("pending_nextion_cmd", cmd);
        }
        delay(700); ESP.restart(); 
      }
      if (tempBuf.length() > 20) tempBuf = ""; 
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); vTaskDelay(50 / portTICK_PERIOD_MS); 
  }
}

void connectWiFi() {
  Serial.printf("\r\n[System] 무선 연결/설정 모드 가동...\r\n"); esp_task_wdt_delete(NULL);
  automaticOfflineMode = false;
  if (prefs.getBool("force_offline", false)) {
    prefs.putBool("force_offline", false); 
    Serial.println(C_YELLOW "\r\n[Offline] Wi-Fi 설정을 건너뛰고 수동 모드로 진입합니다!" C_RESET);
    setOfflineModeActive(true); esp_task_wdt_add(NULL); return; 
  }

  WiFiManager wifiManager;
  // Bound ordinary boot attempts; explicit Wi-Fi setup keeps its existing portal.
  wifiManager.setConnectTimeout(10);
  wifiManager.setSaveConnectTimeout(10);
  wifiManager.setConnectRetries(1);
  bool forceConfigPortal = prefs.getBool("force_config_portal", false);
  if (forceConfigPortal) {
    prefs.putBool("force_config_portal", false); showPage(PAGE_WIFI_RESET); WiFi.disconnect(true, true); delay(200);
  }

  wifiManager.setConfigPortalTimeout(forceConfigPortal ? 0 : 120);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  wifiManager.setCaptivePortalEnable(true); WiFi.setHostname("SmartDiffuser"); wifiManager.setDebugOutput(true);
  WiFi.mode(WIFI_STA); 
  // WiFi.SSID() is empty while disconnected on ESP32; read saved credentials.
  String savedSSID = wifiManager.getWiFiSSID();
  wifiManager.setEnableConfigPortal(forceConfigPortal || savedSSID.length() == 0);

  if (savedSSID.length() > 0) Serial.printf("\r\n[WiFi] 저장된 와이파이 발견: %s\r\n", savedSSID.c_str());
  else Serial.println("\r\n[WiFi] 최초 부팅 상태입니다! (와이파이 미설정)");

  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) { updateDisplay(0, "Setup WiFi"); });

  TaskHandle_t watcherHandle = NULL; clearNextionInputBuffer();
  xTaskCreatePinnedToCore(displayWatcherTask, "WatcherTask", 2048, NULL, 1, &watcherHandle, 0);

  bool wifiConnected = false;
  if (forceConfigPortal) { updateDisplay(0, "Setup WiFi"); wifiConnected = wifiManager.startConfigPortal("SmartDiffuser_Setup"); } 
  else { wifiConnected = wifiManager.autoConnect("SmartDiffuser_Setup"); }

  if (!wifiConnected) {
    Serial.println(C_YELLOW "\r\n[Offline] Wi-Fi unavailable; continuing to local controls.\r\n" C_RESET);
    automaticOfflineMode = true;
    setOfflineModeActive(true);
    wifiRetryInterval = 5000;
    lastWifiRetryMillis = millis();
  }
  else {
    Serial.printf("\r\nConnected! IP: %s\r\n", WiFi.localIP().toString().c_str());
    if (prefs.isKey("wifi_return_page")) {
      int returnPage = prefs.getInt("wifi_return_page", PAGE_WEATHER_OFF); prefs.remove("wifi_return_page");
      if (returnPage == PAGE_WIFI_RESET) returnPage = PAGE_WEATHER_OFF;
      if (returnPage == PAGE_WEATHER_OFF) showWeatherOffPage(); else showPage(returnPage);
    }
  }
  if (watcherHandle != NULL) vTaskDelete(watcherHandle);
  esp_task_wdt_add(NULL);
}

void manageWiFi() {
  static bool wasConnected = false; unsigned long currentTime = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected) { wasConnected = false; Serial.println(C_RED "\r\n🚨 WiFi 끊김! 복구 진입" C_RESET); wifiRetryInterval = 5000; }
    if (currentTime - lastWifiRetryMillis >= wifiRetryInterval) {
      lastWifiRetryMillis = currentTime; WiFi.disconnect(); WiFi.reconnect();
      wifiRetryInterval = min(wifiRetryInterval * 2, 60000UL);
    }
  } else {
    if (!wasConnected) {
      wasConnected = true;
      wifiRetryInterval = 5000;
      // OTA may not have started during an offline boot.
      initOTA();
    }
    if (automaticOfflineMode) {
      automaticOfflineMode = false;
      offlineModeActive = false;
      // Keep a manual/settings page open if the user has already navigated there.
      if (currentDisplayPage == PAGE_OFFLINE) showWeatherOffPage();
      Serial.println(C_GREEN "\r\n[WiFi] Reconnected; online controls restored.\r\n" C_RESET);
    }
  }
}

void pollServer() {
  if (currentMode == MODE_DEMO || isCommunicate) return; 
  unsigned long now = millis();
  if (now - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = now;
    if (WiFi.status() == WL_CONNECTED) {
      JsonDocument doc; doc["action"] = "POLL"; doc["deviceId"] = "SS"; doc["status"] = lastWebMessage;
      MicrophoneLevel snapshot;
      doc["db_level"] = getMicrophoneLevel(snapshot) ? snapshot.level : 0; doc["volume"] = currentVolume / 3; doc["led_br"] = ledEnabled ? ledBrightness : 0;
      int currentPlayingTrack = 0;
      if (currentMode == MODE_AMBIENT) currentPlayingTrack = currentAmbientTrack; 
      else if (isRunning && currentSlotTracksCount > 0) currentPlayingTrack = currentSlotTracks[currentPlaylistIdx];
      doc["music"] = currentPlayingTrack; 

      JsonArray wArray = doc["weights"].to<JsonArray>(); 
      for (int i = 0; i < 4; i++) { float percent = calculateScentPercent(weights[i]); wArray.add(round(percent * 10.0f) / 10.0f); }
      String payload; serializeJson(doc, payload); sendServerRequest(payload); 
    }
  }
}

static void processServerResponse(const String& response, bool isPollRequest = true, bool isWeatherRefreshResponse = false) {
  if (!isPollRequest) return;
  JsonDocument doc; if (deserializeJson(doc, response)) return;

  if (isWeatherRefreshResponse && currentMode != MODE_WEATHER) {
    Serial.println(C_YELLOW "\r\n[Weather] Ignored stale response after leaving weather mode\r\n" C_RESET);
    return;
  }

  // 🟢 1. 앱 명령(마패) 확인 및 LED "최우선" 동기화 (함수 맨 위로 끌어올림!)
  bool isAppCmd = (doc["app_command"] == true || doc["pending_cmd"] == true || doc["command_source"] == "app");
  String serverActiveMode = doc["active_mode"] | ""; serverActiveMode.trim();
  const bool ambientResponse = serverActiveMode == "ambient";
  bool shouldApplySettings = ambientResponse || canApplyServerSettings() || isAppCmd;

  bool hasLedData = !doc["led_r"].isNull() || !doc["ledR"].isNull() || 
                    !doc["led_g"].isNull() || !doc["ledG"].isNull() || 
                    !doc["led_b"].isNull() || !doc["ledB"].isNull() || 
                    !doc["led_bright"].isNull() || !doc["led_br"].isNull();

  // 어떤 상황이든 LED 데이터가 오면 가장 먼저 묻지도 따지지도 않고 바꿉니다!
  if (shouldApplySettings && hasLedData) {
    uint8_t newR = ledR; uint8_t newG = ledG; uint8_t newB = ledB; int newBright = ledBrightness;
    if (!doc["led_r"].isNull()) newR = constrain(doc["led_r"] | newR, 0, 255);
    else if (!doc["ledR"].isNull()) newR = constrain(doc["ledR"] | newR, 0, 255);
    if (!doc["led_g"].isNull()) newG = constrain(doc["led_g"] | newG, 0, 255);
    else if (!doc["ledG"].isNull()) newG = constrain(doc["ledG"] | newG, 0, 255);
    if (!doc["led_b"].isNull()) newB = constrain(doc["led_b"] | newB, 0, 255);
    else if (!doc["ledB"].isNull()) newB = constrain(doc["ledB"] | newB, 0, 255);
    if (!doc["led_bright"].isNull()) newBright = constrain(doc["led_bright"] | newBright, 0, 255);
    else if (!doc["led_br"].isNull()) newBright = constrain(doc["led_br"] | newBright, 0, 255);
    
    ledR = newR; ledG = newG; ledB = newB; ledBrightness = newBright; 
    ledEnabled = newBright > 0 && !(ledR == 0 && ledG == 0 && ledB == 0);
    prefs.putInt("ledBright", ledBrightness); prefs.putUChar("ledR", ledR); prefs.putUChar("ledG", ledG); prefs.putUChar("ledB", ledB); prefs.putBool("ledEnabled", ledEnabled);
    setLedColor(ledEnabled ? ledR : 0, ledEnabled ? ledG : 0, ledEnabled ? ledB : 0);
  }

  // 🟢 2. 모드 및 서버 상태 파싱
  int cmd = doc["spray"] | -1; String resultText = doc["result_text"] | "";

  if (!isAppCmd && currentMode == MODE_WEATHER && wasWeatherStartedLocallyRecently() && serverActiveMode == "manual") {
    Serial.println(C_YELLOW "\r\n[Weather] Ignored stale manual state after local weather start\r\n" C_RESET);
    return;
  }

  String weatherText = doc["weather"] | ""; String targetRegion = doc["target_region"] | "";
  if (targetRegion.length() == 0 && doc.containsKey("region")) targetRegion = doc["region"].as<String>();
  if (targetRegion.length() > 0 && targetRegion != lastWeatherRegion) { rememberWeatherRegion(targetRegion); Serial.printf("\r\n[Sync] 지역이 '%s'로 동기화되었습니다.\r\n", targetRegion.c_str()); }

  if (weatherText.length() > 0) {
    lastWeatherLabel = weatherText; lastWeatherIconId = weatherIconFromText(weatherText);
    hasWeatherSnapshot = hasValidWeatherText(lastWeatherLabel);
    if (currentMode == MODE_WEATHER) updateDisplay(lastWeatherIconId, "");
  }

  float tempC = lastWeatherTempC; float humi = lastWeatherHumi;
  bool hasTemp = readJsonFloatField(doc, "temp", tempC) || readJsonFloatField(doc, "temperature", tempC);
  bool hasHumi = readJsonFloatField(doc, "humidity", humi) || readJsonFloatField(doc, "humid", humi) || readJsonFloatField(doc, "humi", humi);
  if (hasTemp || hasHumi) updateTempHumi(tempC, humi);
  
  refreshWeatherFieldsIfVisible(); showWeatherPageForRefreshResponse(isWeatherRefreshResponse); 

  // 🟢 3. 기타 세팅 및 모드 전환 로직
  if (shouldApplySettings && !doc["intensity"].isNull()) SprayIntensity(doc["intensity"] | 2);
  if (shouldApplySettings && !doc["volume"].isNull()) applyServerVolumeWithoutPageChange(doc["volume"] | 5);

  // Ambient has a strict command contract, independent of manual/weather
  // text heuristics and of whether this response contains a spray command.
  if (ambientResponse || currentMode == MODE_AMBIENT) {
    if (serverActiveMode == "ready" || serverActiveMode == "off") {
      applyServerStop();
      return;
    }
    if (ambientResponse) {
      if (currentMode != MODE_AMBIENT) setSystemMode(MODE_AMBIENT, "Ambient Mode");
      int track = doc["music"] | 0;
      if (track > 0 && track != currentAmbientTrack) {
        playSound(track);
        currentAmbientTrack = track;
        nexSend("t_music.txt=\"" + getTrackName(track) + "\"");
      }
      if (cmd == 0) {
        markLocalStop();
        stopAmbientSpray();
      } else if (doc["app_command"] == true && cmd > 0) {
        int duration = doc["duration"] | 0;
        if (duration > 0 && duration <= MAX_RUN_TIME / 1000) {
          triggerSpray(cmd, duration, 0, resultText, false);
        } else {
          recordControlDiagnostic("ambient-rejected", "invalid-duration");
        }
      }
      if (!doc["timer_enabled"].isNull()) { schedulerEnabled = doc["timer_enabled"] | false; activeStartHour = doc["timer_start"] | 9; activeEndHour = doc["timer_end"] | 22; }
      return;
    }
    if (cmd == 0) {
      markLocalStop();
      stopAmbientSpray();
      return;
    }
    if (serverActiveMode != "manual" && serverActiveMode != "weather") return;
  }

  bool suppressManualSingleDuringBlendSelection = isBlendSelectionInProgress() && cmd >= 1 && cmd <= 4 && serverActiveMode != "weather";
  bool isActiveWeatherRefreshResponse = isWeatherRefreshResponse && currentMode == MODE_WEATHER && isWeatherLikeResponse(serverActiveMode, weatherText, targetRegion, isWeatherRefreshResponse);
  bool isPassiveReadyOrOffStatus = !isAppCmd && cmd <= 0 && resultText.length() == 0 && (serverActiveMode == "ready" || serverActiveMode == "off");
  bool responseHasWeatherData = hasValidWeatherText(weatherText) || hasTemp || hasHumi || targetRegion.length() > 0;
  bool ignorePassiveReadyAfterWeatherStart =
    currentMode == MODE_WEATHER &&
    isPassiveReadyOrOffStatus &&
    (wasWeatherStartedLocallyRecently() || isWeatherRefreshResponse || responseHasWeatherData);

  if (ignorePassiveReadyAfterWeatherStart) {
    Serial.println(C_YELLOW "\r\n[Weather] Ignored passive ready/off status while weather mode is active\r\n" C_RESET);
    return;
  }

  // A status snapshot is not an explicit stop. A late ready/off snapshot must
  // not cancel a manual cycle started on the display. App stops still pass.
  if (currentMode == MODE_MANUAL && isRunning && isPassiveReadyOrOffStatus) {
    Serial.println("[Manual] Ignored passive ready/off status during local spray.");
    return;
  }

  if (!isActiveWeatherRefreshResponse && isServerStopCommand(cmd, resultText, serverActiveMode)) {
    applyServerStop(isExplicitServerStop(cmd, resultText, serverActiveMode, isAppCmd));
    return;
  }

  if (!isAppCmd && wasRecentlyStoppedLocally() && currentMode != MODE_WEATHER && isWeatherLikeResponse(serverActiveMode, weatherText, targetRegion, isWeatherRefreshResponse)) return;

  bool shouldChangeMode = isAppCmd;
  if (serverActiveMode == "weather" && currentMode != MODE_WEATHER) shouldChangeMode = true;
  if (serverActiveMode == "manual" && currentMode != MODE_MANUAL) shouldChangeMode = true;

  if (shouldChangeMode) {
    if (serverActiveMode == "weather") {
      if (currentMode != MODE_WEATHER) {
        stopSystem();
        setSystemMode(MODE_WEATHER, "Weather Mode"); showWeatherPageByState(); requestWeatherRefresh("");
      }
    } 
    else if (serverActiveMode == "manual") {
      int activeScent = doc["active_scent"] | 0; if (activeScent <= 0) activeScent = doc["spray"] | 0;
      suppressManualSingleDuringBlendSelection = suppressManualSingleDuringBlendSelection || (isBlendSelectionInProgress() && activeScent >= 1 && activeScent <= 4 && !isValidBlendCommand(activeScent));
      if (!suppressManualSingleDuringBlendSelection && ((activeScent >= 1 && activeScent <= 4) || isValidBlendCommand(activeScent))) {
        if (!isAppCmd && lastStoppedManualScent == activeScent && millis() - manualModeOffMillis < 5000) return;
        bool scentChanged = activeScent != lastSyncedManualScent; lastSyncedManualScent = activeScent;
        if ((currentMode != MODE_MANUAL || scentChanged) && !isRunningManualCommand(activeScent)) {
          stopSystem();
          setSystemMode(MODE_MANUAL, "Manual Mode"); showManualPageForServerScent(activeScent);
        }
      } else if (currentMode != MODE_MANUAL) {
        stopSystem();
        setSystemMode(MODE_MANUAL, "Manual Mode"); showManualPageByState();
      }
    }
  }

  if (!doc["music_tracks"].isNull()) updateMusicMapping(doc["music_tracks"] | "");
  bool shouldRunSpray = cmd > 0;
  if (suppressManualSingleDuringBlendSelection && cmd >= 1 && cmd <= 4) shouldRunSpray = false;

  if (shouldRunSpray) {
    int dur = doc["duration"] | 3; int music = doc["music"] | 0; String txt = resultText.length() > 0 ? resultText : "명령 수신";
    if (serverActiveMode == "manual" && isRunningManualCommand(cmd)) {
      Serial.println("[Manual] Same command already running; keeping spray/rest phase.");
    } else {
      triggerSpray(cmd, dur, music, txt, (currentMode == MODE_WEATHER));
    }
  } else if (!suppressManualSingleDuringBlendSelection && !doc["music"].isNull() && doc["music"] > 0) {
    int track = doc["music"] | 0; playSound(track); nexSend("t_music.txt=\"" + getTrackName(track) + "\""); updateDisplay(0, "Music Playing");
  }

  if (!doc["timer_enabled"].isNull()) { schedulerEnabled = doc["timer_enabled"] | false; activeStartHour = doc["timer_start"] | 9; activeEndHour = doc["timer_end"] | 22; }
}

void processPendingServerResponse() {
  if (!serverResponseQueue) return;
  PendingServerResponse *message = nullptr;
  if (xQueueReceive(serverResponseQueue, &message, 0) != pdTRUE) return;
  if (!message->weatherRefresh && !message->weatherRetry) {
    Serial.println("[POLL raw] " + message->body);
  }
  if (message->stopGeneration != currentStopGeneration()) {
    // Stops win even when their request predates a local stop. Old starts and
    // settings never reach the actuator/display handlers after the barrier.
    if (!message->weatherRetry && isExplicitServerStopResponse(message->body)) {
      JsonDocument stale;
      deserializeJson(stale, message->body);
      if (currentMode == MODE_AMBIENT && stale["active_mode"] == "ambient" && stale["spray"] == 0) {
        markLocalStop();
        stopAmbientSpray();
      } else applyServerStop();
    }
    else {
      recordControlDiagnostic("old-response", "discarded");
      Serial.println("[Network] Ignored response requested before the latest stop.");
    }
  }
  else if (message->weatherRetry) allowWeatherRetry();
  else processServerResponse(message->body, true, message->weatherRefresh);
  delete message;
}

void networkTaskLoop(void *pvParameters) {
  if (!networkQueue || !serverResponseQueue) { vTaskDelete(NULL); return; }
  for (;;) {
    PendingServerRequest* currentMsgPtr;
    if (xQueueReceive(networkQueue, &currentMsgPtr, portMAX_DELAY) == pdTRUE) {
      String payload = currentMsgPtr->body;
      const uint32_t requestGeneration = currentMsgPtr->stopGeneration;
      delete currentMsgPtr;
      bool isWeatherRefreshResponse = isWeatherRequestPayload(payload); bool shouldProcessResponse = shouldProcessNetworkResponse(payload);
      isCommunicate = true; initNetworkSession(); HTTPClient http; http.setTimeout(20000);
      
      const bool isPoll = payload.indexOf("\"POLL\"") >= 0;
      if (http.begin(sharedClient, serverName)) {
        http.addHeader("Content-Type", "application/json");
        if (isPoll) beginPollDiagnostic(payload);
        int httpCode = http.POST(payload);
        String response;
        if (httpCode == HTTP_CODE_OK || (isPoll && httpCode > 0)) response = http.getString();
        if (isPoll) finishPollDiagnostic(response, httpCode);
        if (httpCode == HTTP_CODE_OK) { if (shouldProcessResponse) queueServerResponse(response, isWeatherRefreshResponse, requestGeneration); }
        else { if (isWeatherRefreshResponse) queueServerResponse("", true, requestGeneration, true); Serial.printf(C_RED "⚠️ [Network] HTTP 에러: %d\r\n" C_RESET, httpCode); }
        http.end();
      } else { if (isWeatherRefreshResponse) queueServerResponse("", true, requestGeneration, true); }
      isCommunicate = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void sendServerRequest(String payload) {
  if (WiFi.status() != WL_CONNECTED || !networkQueue || !serverResponseQueue) return;
  const uint32_t requestGeneration = currentStopGeneration();
  String normalizedPayload = attachDeviceIdIfMissing(payload);
  PendingServerRequest* msg = new (std::nothrow) PendingServerRequest{normalizedPayload, requestGeneration};
  if (!msg || msg->body.length() != normalizedPayload.length()) {
    delete msg;
    Serial.println("[Network] Unable to allocate server request.");
    return;
  }
  if (xQueueSend(networkQueue, &msg, 0) != pdTRUE) { delete msg; Serial.println(C_RED "⚠️ 통신 대기열 꽉 참" C_RESET); }
}

bool recordAudio(uint8_t** audioBuffer, uint32_t* totalSize) {
  uint32_t dataSize = SAMPLE_RATE * RECORD_TIME * 2; *totalSize = sizeof(WavHeader) + dataSize;
  if (ESP.getFreeHeap() < *totalSize + 20000) { updateDisplay(0, "Memory Full"); return false; }
  Serial.printf(C_YELLOW "\r\n[Voice] 🎤 녹음 시작...\r\n" C_RESET);
  *audioBuffer = (uint8_t*)heap_caps_malloc(*totalSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL); if (*audioBuffer == NULL) return false;
  WavHeader header; memcpy(header.riff, "RIFF", 4); header.overall_size = *totalSize - 8; memcpy(header.wave, "WAVE", 4); memcpy(header.fmt_chunk_marker, "fmt ", 4); 
  header.length_of_fmt = 16; header.format_type = 1; header.channels = 1; header.sample_rate = SAMPLE_RATE; header.byterate = SAMPLE_RATE * 2; 
  header.block_align = 2; header.bits_per_sample = 16; memcpy(header.data_chunk_header, "data", 4); header.data_size = dataSize; memcpy(*audioBuffer, &header, sizeof(WavHeader));
  int16_t *pcm = reinterpret_cast<int16_t*>(*audioBuffer + sizeof(WavHeader));
  if (!recordMicrophonePcm(pcm, dataSize / sizeof(int16_t), RECORD_TIME * 1000UL + 1000UL)) {
    free(*audioBuffer);
    *audioBuffer = nullptr;
    *totalSize = 0;
    return false;
  }
  return true;
}

void executeVoiceCommand(String jsonResponse) {
  JsonDocument doc; if (deserializeJson(doc, jsonResponse)) return;
  String transcript = doc["transcript"] | "인식 실패"; String resultText = doc["result_text"] | "명령 수신";
  int cmd = doc["spray"] | -1; int dur = doc["duration"] | 3; int music = doc["music"] | 0;
  if (cmd >= 1 && cmd <= 4) triggerSpray(cmd, dur, music, resultText, false); else if (cmd == 0) { stopSystem(); updateDisplay(0, resultText); }
}

bool sendAudioToServer(uint8_t* audioBuffer, uint32_t totalSize, String& response) {
  if (isCommunicate) return false; isCommunicate = true; updateDisplay(0, "Thinking..."); esp_task_wdt_delete(NULL); bool success = false; initNetworkSession();
  HTTPClient http; http.setTimeout(15000); 
  if (http.begin(sharedClient, serverName)) { http.addHeader("Content-Type", "audio/wav"); int httpCode = http.POST(audioBuffer, totalSize); if (httpCode > 0) { response.reserve(512); response = http.getString(); success = true; } http.end(); }
  esp_task_wdt_add(NULL); isCommunicate = false; return success;
}

void recordAndSendVoice() {
  if (WiFi.status() != WL_CONNECTED) return;
  SystemMode prevMode = currentMode; currentMode = MODE_VOICE; uint8_t* audioBuffer = NULL; uint32_t totalSize = 0;
  if (recordAudio(&audioBuffer, &totalSize)) { String response = ""; if (sendAudioToServer(audioBuffer, totalSize, response)) executeVoiceCommand(response); }
  if (audioBuffer != NULL) free(audioBuffer);
  // A stop or mode selection during recording must not be overwritten here.
  if (currentMode == MODE_VOICE) currentMode = (prevMode == MODE_VOICE) ? MODE_READY : prevMode;
  showPrompt();
}

void autoWeatherScheduler() { if (currentMode != MODE_WEATHER || isRunning) return; if (millis() - lastWeatherCallMillis >= WEATHER_INTERVAL) requestWeatherRefresh(""); }
static String buildControlDiagnosticJson() {
  ControlDiagnosticEvent events[DIAGNOSTIC_EVENT_COUNT];
  char lastCommand[sizeof(lastNextionCommand)];
  size_t count;
  uint32_t stopCount;
  portENTER_CRITICAL(&diagnosticMux);
  count = diagnosticCount;
  stopCount = nextionStopCount;
  memcpy(lastCommand, lastNextionCommand, sizeof(lastCommand));
  size_t oldest = (diagnosticWriteIndex + DIAGNOSTIC_EVENT_COUNT - count) % DIAGNOSTIC_EVENT_COUNT;
  for (size_t i = 0; i < count; ++i) events[i] = diagnosticEvents[(oldest + i) % DIAGNOSTIC_EVENT_COUNT];
  portEXIT_CRITICAL(&diagnosticMux);

  JsonDocument doc;
  doc["diagnostic_version"] = 3;
  doc["poll_device_id"] = "SS";
  PollDiagnostic pollSnapshot;
  const bool pollSnapshotAvailable = pollDiagnosticMutex && xSemaphoreTake(pollDiagnosticMutex, 0) == pdTRUE;
  if (pollSnapshotAvailable) {
    pollSnapshot = pollDiagnostic;
    xSemaphoreGive(pollDiagnosticMutex);
    doc["last_poll_request"] = pollSnapshot.request;
    doc["last_poll_request_at_ms"] = pollSnapshot.requestAt;
    doc["last_poll_response"] = pollSnapshot.response;
    doc["last_poll_response_at_ms"] = pollSnapshot.responseAt;
    doc["poll_sequence"] = pollSnapshot.sequence;
    doc["poll_http_code"] = pollSnapshot.httpCode;
    doc["poll_in_flight"] = pollSnapshot.inFlight;
  }
  doc["poll_snapshot_available"] = pollSnapshotAvailable;
  doc["noise_level_unit"] = "relative_pcm_rms";
  doc["noise_level_note"] = "상대 소음 레벨 (보정된 dB SPL 아님)";
  doc["own_bgm_may_affect_noise"] = true;
  doc["measurement_muting_enabled"] = false;
  doc["measurement_note"] = "반복 음소거 없음. 자체 BGM이 마이크 측정값에 포함될 수 있음.";
  MicrophoneLevel microphoneSnapshot;
  doc["microphone_valid"] = getMicrophoneLevel(microphoneSnapshot);
  doc["ambient_pump_safety_limit_ms"] = MAX_RUN_TIME;
  doc["ambient_pump_safety_scope"] = "per_server_spray_excludes_waiting";
  doc["db_level"] = currentDbLevel;
  doc["ambient_track"] = currentAmbientTrack;
  doc["spray_duration_ms"] = sprayDuration;
  doc["build"] = String(__DATE__) + " " + String(__TIME__);
  doc["deviceId"] = deviceId;
  doc["uptime_ms"] = millis();
  doc["mode"] = static_cast<int>(currentMode);
  doc["running"] = isRunning;
  doc["spraying"] = isSpraying;
  doc["last_nextion_command"] = lastCommand;
  doc["nextion_stop_count"] = stopCount;
  doc["stop_generation"] = currentStopGeneration();
  JsonArray gpio = doc["gpio_high"].to<JsonArray>();
  JsonArray selected = doc["active_nozzles"].to<JsonArray>();
  JsonArray sensors = doc["sensor_valid"].to<JsonArray>();
  uint8_t gpioMask = currentSprayGpioMask();
  for (int i = 0; i < 4; ++i) {
    gpio.add((gpioMask & (1U << i)) != 0);
    selected.add(activeNozzles[i]);
    sensors.add(isLoadcellMeasurementValid(i));
  }
  JsonArray history = doc["events"].to<JsonArray>();
  for (size_t i = 0; i < count; ++i) {
    JsonObject entry = history.add<JsonObject>();
    entry["at_ms"] = events[i].atMillis;
    entry["event"] = events[i].event;
    entry["detail"] = events[i].detail;
    entry["mode"] = events[i].mode;
    entry["running"] = events[i].running;
    entry["spraying"] = events[i].spraying;
    JsonArray levels = entry["gpio_high"].to<JsonArray>();
    JsonArray active = entry["active_nozzles"].to<JsonArray>();
    for (int channel = 0; channel < 4; ++channel) {
      levels.add((events[i].gpioMask & (1U << channel)) != 0);
      active.add((events[i].activeMask & (1U << channel)) != 0);
    }
  }
  String body;
  serializeJson(doc, body);
  return body;
}

void handleWebClient() {
  static WiFiClient client;
  static String transmitBuffer;
  static size_t transmitted = 0;
  static unsigned long acceptedAt = 0;
  static uint32_t headerTail = 0;
  static size_t headerBytes = 0;
  static char requestLine[128] = {};
  static size_t requestLength = 0;
  static bool requestLineComplete = false;
  static bool headersComplete = false;
  auto closeClient = [&]() {
    client.stop();
    transmitBuffer = "";
    transmitted = 0;
  };
  if (!client) {
    client = webServer.available();
    if (!client) return;
    acceptedAt = millis();
    headerTail = 0;
    headerBytes = 0;
    requestLength = 0;
    requestLine[0] = '\0';
    requestLineComplete = false;
    headersComplete = false;
    transmitBuffer = "";
    transmitted = 0;
  }
  if (millis() - acceptedAt > 1000UL) { closeClient(); return; }
  if (!headersComplete) {
    // Bounded parsing: diagnostics must never wait for a browser or delay ST.
    int budget = 128;
    while (budget-- > 0 && client.available()) {
      int next = client.read();
      if (next < 0) break;
      if (++headerBytes > 2048) { closeClient(); return; }
      if (!requestLineComplete) {
        if (next == '\n') requestLineComplete = true;
        else if (next != '\r') {
          if (requestLength + 1 >= sizeof(requestLine)) { closeClient(); return; }
          requestLine[requestLength++] = static_cast<char>(next);
          requestLine[requestLength] = '\0';
        }
      }
      headerTail = (headerTail << 8) | static_cast<uint8_t>(next);
      if (headerTail == 0x0d0a0d0aUL) { headersComplete = true; break; }
    }
    if (!headersComplete) return;
    bool readOnlyGet = strncmp(requestLine, "GET ", 4) == 0;
    bool supportedPath = strncmp(requestLine, "GET / ", 6) == 0 || strncmp(requestLine, "GET /diagnostics ", 17) == 0;
    String body = readOnlyGet && supportedPath ? buildControlDiagnosticJson() : "{\"error\":\"Read-only GET /diagnostics\"}";
    transmitBuffer = readOnlyGet && supportedPath ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n";
    transmitBuffer += "Content-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ";
    transmitBuffer += String(body.length()) + "\r\n\r\n" + body;
  }
  size_t remaining = transmitBuffer.length() - transmitted;
  size_t chunk = remaining < 512 ? remaining : 512;
  // NetworkClient::write can retry for seconds. A single nonblocking socket
  // send per loop keeps display input and spray safety running normally.
  int written = send(client.fd(), transmitBuffer.c_str() + transmitted, chunk, MSG_DONTWAIT);
  if (written > 0) transmitted += static_cast<size_t>(written);
  else if (written == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { closeClient(); return; }
  if (transmitted == transmitBuffer.length()) closeClient();
}
void initOTA() { 
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname("SmartDiffuser"); ArduinoOTA.setPassword("1234"); 
  ArduinoOTA.onStart([]() { esp_task_wdt_delete(NULL); forceAllOff(); updateDisplay(0, "OTA Updating..."); });
  ArduinoOTA.onEnd([]() { prefs.putBool("force_startup_page", true); updateDisplay(0, "Update Success!"); esp_task_wdt_add(NULL); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { updateDisplay(0, String("OTA ") + String(progress / (total / 100)) + "%"); });
  ArduinoOTA.onError([](ota_error_t error) { delay(1500); ESP.restart(); }); ArduinoOTA.begin(); 
}
void handleOTA() { ArduinoOTA.handle(); }
