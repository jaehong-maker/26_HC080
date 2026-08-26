#include "Globals.h"

static WiFiClientSecure sharedClient;
static bool isSessionInit = false;

static unsigned long lastLocalSettingsMillis = 0;
static const unsigned long LOCAL_SETTINGS_PROTECT_MS = 30000;
static unsigned long lastLocalStopMillis = 0;
static const unsigned long WEATHER_RESPONSE_IGNORE_AFTER_STOP_MS = 15000;

void markLocalSettingsChanged() { lastLocalSettingsMillis = millis(); }
void markLocalStop() { lastLocalStopMillis = millis(); }
void clearLocalStopMark() { lastLocalStopMillis = 0; }

static bool canApplyServerSettings() {
  return lastLocalSettingsMillis == 0 || (millis() - lastLocalSettingsMillis >= LOCAL_SETTINGS_PROTECT_MS);
}
static bool wasRecentlyStoppedLocally() {
  return lastLocalStopMillis > 0 && millis() - lastLocalStopMillis < WEATHER_RESPONSE_IGNORE_AFTER_STOP_MS;
}

static bool isServerStopCommand(int cmd, const String &resultText, const String &serverActiveMode) {
  if (serverActiveMode == "ready" || serverActiveMode == "off") return true;
  if (cmd == 90) return true;
  if (resultText == "STOP" || resultText == "OFF") return true;
  if (resultText.indexOf("중지") >= 0 || resultText.indexOf("정지") >= 0) return true;
  if (resultText.indexOf("꺼짐") >= 0 || resultText.indexOf("대기") >= 0) return true;
  return false; 
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
  if (doc["deviceId"].isNull()) doc["deviceId"] = deviceId;
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
  if (prefs.getBool("force_offline", false)) {
    prefs.putBool("force_offline", false); 
    Serial.println(C_YELLOW "\r\n[Offline] Wi-Fi 설정을 건너뛰고 수동 모드로 진입합니다!" C_RESET);
    setOfflineModeActive(true); esp_task_wdt_add(NULL); return; 
  }

  WiFiManager wifiManager;
  bool forceConfigPortal = prefs.getBool("force_config_portal", false);
  if (forceConfigPortal) {
    prefs.putBool("force_config_portal", false); showPage(PAGE_WIFI_RESET); WiFi.disconnect(true, true); delay(200);
  }

  wifiManager.setConfigPortalTimeout(forceConfigPortal ? 0 : 120);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  wifiManager.setCaptivePortalEnable(true); WiFi.setHostname("SmartDiffuser"); wifiManager.setDebugOutput(true);
  WiFi.mode(WIFI_STA); 
  String savedSSID = WiFi.SSID();

  if (savedSSID.length() > 0) Serial.printf("\r\n[WiFi] 저장된 와이파이 발견: %s\r\n", savedSSID.c_str());
  else Serial.println("\r\n[WiFi] 최초 부팅 상태입니다! (와이파이 미설정)");

  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) { updateDisplay(0, "Setup WiFi"); });

  TaskHandle_t watcherHandle = NULL; clearNextionInputBuffer();
  xTaskCreatePinnedToCore(displayWatcherTask, "WatcherTask", 2048, NULL, 1, &watcherHandle, 0);

  bool wifiConnected = false;
  if (forceConfigPortal) { updateDisplay(0, "Setup WiFi"); wifiConnected = wifiManager.startConfigPortal("SmartDiffuser_Setup"); } 
  else { wifiConnected = wifiManager.autoConnect("SmartDiffuser_Setup"); }

  if (!wifiConnected) Serial.println(C_YELLOW "\r\n[Warning] Wi-Fi 설정 실패." C_RESET);
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
  static bool wasConnected = true; unsigned long currentTime = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected) { wasConnected = false; Serial.println(C_RED "\r\n🚨 WiFi 끊김! 복구 진입" C_RESET); wifiRetryInterval = 5000; }
    if (currentTime - lastWifiRetryMillis >= wifiRetryInterval) {
      lastWifiRetryMillis = currentTime; WiFi.disconnect(); WiFi.reconnect();
      wifiRetryInterval = min(wifiRetryInterval * 2, 60000UL);
    }
  } else { if (!wasConnected) { wasConnected = true; wifiRetryInterval = 5000; } }
}

void pollServer() {
  if (currentMode == MODE_DEMO || isCommunicate) return; 
  unsigned long now = millis();
  static unsigned long lastSampleTime = 0; static int dbSamples[5] = {0, 0, 0, 0, 0}; static int sampleIdx = 0;

  if (now - lastSampleTime >= 3000) { lastSampleTime = now; dbSamples[sampleIdx] = currentDbLevel; sampleIdx = (sampleIdx + 1) % 5; }

  if (now - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = now; int dbSum = 0; for(int i = 0; i < 5; i++) dbSum += dbSamples[i];
    int avgDb = dbSum / 5;
    
    if (WiFi.status() == WL_CONNECTED) {
      JsonDocument doc; doc["action"] = "POLL"; doc["deviceId"] = deviceId; doc["status"] = lastWebMessage; 
      doc["db_level"] = avgDb; doc["volume"] = currentVolume / 3; doc["led_br"] = ledEnabled ? ledBrightness : 0;
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

  static int lastSyncedManualScent = 0;
  if (canApplyServerSettings() && !doc["intensity"].isNull()) SprayIntensity(doc["intensity"] | 2);
  if (canApplyServerSettings() && !doc["volume"].isNull()) applyServerVolumeWithoutPageChange(doc["volume"] | 5);

  bool isAppCmd = (doc["app_command"] == true || doc["pending_cmd"] == true || doc["command_source"] == "app");
  String serverActiveMode = doc["active_mode"] | ""; serverActiveMode.trim();
  int cmd = doc["spray"] | -1; String resultText = doc["result_text"] | "";
  
  bool suppressManualSingleDuringBlendSelection = isBlendSelectionInProgress() && cmd >= 1 && cmd <= 4 && serverActiveMode != "weather";
  bool isActiveWeatherRefreshResponse = isWeatherRefreshResponse && currentMode == MODE_WEATHER && isWeatherLikeResponse(serverActiveMode, weatherText, targetRegion, isWeatherRefreshResponse);

  if (!isActiveWeatherRefreshResponse && isServerStopCommand(cmd, resultText, serverActiveMode)) {
    SystemMode modeBeforeStop = currentMode; markLocalStop(); clearWeatherState(); setSystemMode(MODE_READY, "Stopped by Server");
    if (modeBeforeStop == MODE_WEATHER || currentDisplayPage == PAGE_WEATHER) showWeatherOffPage(); else updateDisplay(0, "Stopped by Server");
    lastSyncedManualScent = 0; manualModeOffMillis = 0; lastStoppedManualScent = 0; Serial.println(C_YELLOW "\r\n🛑 [Server Sync] 서버 정지 신호 수신\r\n" C_RESET); return;
  }

  if (wasRecentlyStoppedLocally() && currentMode != MODE_WEATHER && isWeatherLikeResponse(serverActiveMode, weatherText, targetRegion, isWeatherRefreshResponse)) return;

  bool shouldChangeMode = isAppCmd;
  if (serverActiveMode == "weather" && currentMode != MODE_WEATHER) shouldChangeMode = true;
  if (serverActiveMode == "manual" && currentMode != MODE_MANUAL) shouldChangeMode = true;

  if (shouldChangeMode) {
    if (serverActiveMode == "weather") {
      if (currentMode != MODE_WEATHER) { setSystemMode(MODE_WEATHER, "Weather Mode"); showWeatherPageByState(); requestWeatherRefresh(lastWeatherRegion); }
    } 
    else if (serverActiveMode == "manual") {
      int activeScent = doc["active_scent"] | 0; if (activeScent <= 0) activeScent = doc["spray"] | 0;
      suppressManualSingleDuringBlendSelection = suppressManualSingleDuringBlendSelection || (isBlendSelectionInProgress() && activeScent >= 1 && activeScent <= 4 && !isValidBlendCommand(activeScent));
      if (!suppressManualSingleDuringBlendSelection && ((activeScent >= 1 && activeScent <= 4) || isValidBlendCommand(activeScent))) {
        if (lastStoppedManualScent == activeScent && millis() - manualModeOffMillis < 5000) return;
        bool scentChanged = activeScent != lastSyncedManualScent; lastSyncedManualScent = activeScent;
        if (currentMode != MODE_MANUAL || scentChanged) { setSystemMode(MODE_MANUAL, "Manual Mode"); showManualPageForServerScent(activeScent); }
      } else if (currentMode != MODE_MANUAL) { setSystemMode(MODE_MANUAL, "Manual Mode"); showManualPageByState(); }
    }
  }

  if (!doc["music_tracks"].isNull()) updateMusicMapping(doc["music_tracks"] | "");
  bool shouldRunSpray = cmd > 0;
  if (suppressManualSingleDuringBlendSelection && cmd >= 1 && cmd <= 4) shouldRunSpray = false;

  if (shouldRunSpray) {
    int dur = doc["duration"] | 3; int music = doc["music"] | 0; String txt = resultText.length() > 0 ? resultText : "명령 수신";
    triggerSpray(cmd, dur, music, txt, (currentMode == MODE_WEATHER));
  } else if (!suppressManualSingleDuringBlendSelection && !doc["music"].isNull() && doc["music"] > 0) {
    int track = doc["music"] | 0; playSound(track); nexSend("t_music.txt=\"" + getTrackName(track) + "\""); updateDisplay(0, "Music Playing");
  }

  if (!doc["timer_enabled"].isNull()) { schedulerEnabled = doc["timer_enabled"] | false; activeStartHour = doc["timer_start"] | 9; activeEndHour = doc["timer_end"] | 22; }

  if (canApplyServerSettings() && !doc["led_r"].isNull() && !doc["led_g"].isNull() && !doc["led_b"].isNull()) {
    uint8_t newR = constrain(doc["led_r"] | 255, 0, 255); uint8_t newG = constrain(doc["led_g"] | 255, 0, 255); uint8_t newB = constrain(doc["led_b"] | 255, 0, 255);
    int newBright = ledBrightness; if (!doc["led_bright"].isNull()) newBright = constrain(doc["led_bright"] | 150, 0, 255); else if (!doc["led_br"].isNull()) newBright = constrain(doc["led_br"] | 150, 0, 255);
    ledR = newR; ledG = newG; ledB = newB; ledBrightness = newBright; ledEnabled = newBright > 0 && !(ledR == 0 && ledG == 0 && ledB == 0);
    prefs.putInt("ledBright", ledBrightness); prefs.putUChar("ledR", ledR); prefs.putUChar("ledG", ledG); prefs.putUChar("ledB", ledB); prefs.putBool("ledEnabled", ledEnabled);
    setLedColor(ledEnabled ? ledR : 0, ledEnabled ? ledG : 0, ledEnabled ? ledB : 0);
  }
}

void networkTaskLoop(void *pvParameters) {
  for (;;) {
    String* currentMsgPtr;
    if (xQueueReceive(networkQueue, &currentMsgPtr, portMAX_DELAY) == pdTRUE) {
      String payload = *currentMsgPtr; delete currentMsgPtr;
      bool isWeatherRefreshResponse = isWeatherRequestPayload(payload); bool shouldProcessResponse = shouldProcessNetworkResponse(payload);
      isCommunicate = true; initNetworkSession(); HTTPClient http; http.setTimeout(20000);
      
      if (http.begin(sharedClient, serverName)) {
        http.addHeader("Content-Type", "application/json"); int httpCode = http.POST(payload);
        if (httpCode == HTTP_CODE_OK) { String response = http.getString(); processServerResponse(response, shouldProcessResponse, isWeatherRefreshResponse); } 
        else { if (isWeatherRequestPayload(payload)) allowWeatherRetry(); Serial.printf(C_RED "⚠️ [Network] HTTP 에러: %d\r\n" C_RESET, httpCode); }
        http.end();
      } else { if (isWeatherRequestPayload(payload)) allowWeatherRetry(); }
      isCommunicate = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void sendServerRequest(String payload) {
  if (WiFi.status() != WL_CONNECTED) return;
  String normalizedPayload = attachDeviceIdIfMissing(payload); String* msg = new String(normalizedPayload);
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
  size_t bytesRead = 0; i2s_read(I2S_PORT, (void*)(*audioBuffer + sizeof(WavHeader)), dataSize, &bytesRead, portMAX_DELAY);
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
  if (audioBuffer != NULL) free(audioBuffer); currentMode = (prevMode == MODE_VOICE) ? MODE_READY : prevMode; showPrompt();
}

void autoWeatherScheduler() { if (currentMode != MODE_WEATHER || isRunning) return; if (millis() - lastWeatherCallMillis >= WEATHER_INTERVAL) requestWeatherRefresh(lastWeatherRegion); }
void handleWebClient() { WiFiClient client = webServer.available(); if (!client) return; client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n"); client.stop(); }
void initOTA() { 
  ArduinoOTA.setHostname("SmartDiffuser"); ArduinoOTA.setPassword("1234"); 
  ArduinoOTA.onStart([]() { esp_task_wdt_delete(NULL); forceAllOff(); updateDisplay(0, "OTA Updating..."); });
  ArduinoOTA.onEnd([]() { prefs.putBool("force_startup_page", true); updateDisplay(0, "Update Success!"); esp_task_wdt_add(NULL); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { updateDisplay(0, String("OTA ") + String(progress / (total / 100)) + "%"); });
  ArduinoOTA.onError([](ota_error_t error) { delay(1500); ESP.restart(); }); ArduinoOTA.begin(); 
}
void handleOTA() { ArduinoOTA.handle(); }
