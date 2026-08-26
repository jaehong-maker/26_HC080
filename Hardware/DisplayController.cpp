#include "Globals.h"

// --- 전역 UI 상태 변수 실체화 ---
bool hasWeatherSnapshot = false;
bool hasTempHumiSnapshot = false;
float lastWeatherTempC = 0.0f;
float lastWeatherHumi = 0.0f;
int currentDisplayPage = PAGE_WEATHER_OFF;
unsigned long manualModeOffMillis = 0;
int lastStoppedManualScent = 0;
bool offlineModeActive = false;
String pendingManualScent = "";

// --- 내부 정적 변수 ---
static const int VOLUME_PAGE_BASE = 58;
static const int VOLUME_LEVEL_MIN = 0;
static const int VOLUME_LEVEL_MAX = 10;
static const int TEMP_FONT_NORMAL = 0;
static const int TEMP_FONT_LOADING = 7;

static int lastManualPage = PAGE_MANUAL;
static bool pendingPageUpdate = false;
static unsigned long pageTransitionTime = 0;
static int transitioningPageId = 0;
static bool pendingWeatherFieldUpdate = false;
static bool pendingWeatherLoadingUpdate = false;

static unsigned long lastScentTouchTime = 0;
static bool volumeNeedsSave = false;
static unsigned long lastVolumeChangeTime = 0;

// --- 구조체 및 지역/날씨 데이터 인코딩 상수 ---
struct EncodedRegionText { const char *utf8; const uint8_t *encoded; size_t length; };
struct EncodedWeatherText { const char *utf8; const uint8_t *encoded; size_t length; };

static const uint8_t REGION_SEOUL[]    = {0xBC, 0xAD, 0xBF, 0xEF, 0x3A};
static const uint8_t REGION_SUWON[]    = {0xBC, 0xF6, 0xBF, 0xF8, 0x3A};
static const uint8_t REGION_SEONGNAM[] = {0xBC, 0xBA, 0xB3, 0xB2, 0x3A};
static const uint8_t REGION_ANYANG[]   = {0xBE, 0xC8, 0xBE, 0xE7, 0x3A};
static const uint8_t REGION_GOYANG[]   = {0xB0, 0xED, 0xBE, 0xE7, 0x3A};
static const uint8_t REGION_YONGIN[]   = {0xBF, 0xEB, 0xC0, 0xCE, 0x3A};
static const uint8_t REGION_BUCHEON[]  = {0xBA, 0xCE, 0xC3, 0xB5, 0x3A};
static const uint8_t REGION_ANSAN[]    = {0xBE, 0xC8, 0xBB, 0xEA, 0x3A};
static const uint8_t REGION_NAMYANGJU[] = {0xB3, 0xB2, 0xBE, 0xE7, 0xC1, 0xD6, 0x3A};
static const uint8_t REGION_HWASEONG[] = {0xC8, 0xAD, 0xBC, 0xBA, 0x3A};
static const uint8_t REGION_PYEONGTAEK[] = {0xC6, 0xF2, 0xC5, 0xC3, 0x3A};
static const uint8_t REGION_UIJEONGBU[] = {0xC0, 0xC7, 0xC1, 0xA4, 0xBA, 0xCE, 0x3A};
static const uint8_t REGION_PAJU[]     = {0xC6, 0xC4, 0xC1, 0xD6, 0x3A};
static const uint8_t REGION_GIMPO[]    = {0xB1, 0xE8, 0xC6, 0xF7, 0x3A};
static const uint8_t REGION_GWANGMYEONG[] = {0xB1, 0xA4, 0xB8, 0xED, 0x3A};
static const uint8_t REGION_GWANGJU[]  = {0xB1, 0xA4, 0xC1, 0xD6, 0x3A};
static const uint8_t REGION_ICHEON[]   = {0xC0, 0xCC, 0xC3, 0xB5, 0x3A};
static const uint8_t REGION_YANGJU[]   = {0xBE, 0xE7, 0xC1, 0xD6, 0x3A};
static const uint8_t REGION_GURI[]     = {0xB1, 0xB8, 0xB8, 0xAE, 0x3A};
static const uint8_t REGION_POCHEON[]  = {0xC6, 0xF7, 0xC3, 0xB5, 0x3A};
static const uint8_t REGION_YANGPYEONG[] = {0xBE, 0xE7, 0xC6, 0xF2, 0x3A};
static const uint8_t REGION_GAPYEONG[] = {0xB0, 0xA1, 0xC6, 0xF2, 0x3A};
static const uint8_t REGION_SIHEUNG[]  = {0xBD, 0xC3, 0xC8, 0xEF, 0x3A};
static const uint8_t REGION_INCHEON[]  = {0xC0, 0xCE, 0xC3, 0xB5, 0x3A};
static const uint8_t REGION_GANGWON[]  = {0xB0, 0xAD, 0xBF, 0xF8, 0x3A};
static const uint8_t REGION_CHUNGBUK[] = {0xC3, 0xE6, 0xBA, 0xCF, 0x3A};
static const uint8_t REGION_CHUNGNAM[] = {0xC3, 0xE6, 0xB3, 0xB2, 0x3A};
static const uint8_t REGION_DAEJEON[]  = {0xB4, 0xEB, 0xC0, 0xFC, 0x3A};
static const uint8_t REGION_SEJONG[]   = {0xBC, 0xBC, 0xC1, 0xBE, 0x3A};
static const uint8_t REGION_JEONBUK[]  = {0xC0, 0xFC, 0xBA, 0xCF, 0x3A};
static const uint8_t REGION_JEONNAM[]  = {0xC0, 0xFC, 0xB3, 0xB2, 0x3A};
static const uint8_t REGION_DAEGU[]    = {0xB4, 0xEB, 0xB1, 0xB8, 0x3A};
static const uint8_t REGION_GYEONGBUK[] = {0xB0, 0xE6, 0xBA, 0xCF, 0x3A};
static const uint8_t REGION_ULLEUNGDO[] = {0xBF, 0xEF, 0xB8, 0xAA, 0xB5, 0xB5, 0x3A};
static const uint8_t REGION_GYEONGNAM[] = {0xB0, 0xE6, 0xB3, 0xB2, 0x3A};
static const uint8_t REGION_BUSAN[]    = {0xBA, 0xCE, 0xBB, 0xEA, 0x3A};
static const uint8_t REGION_ULSAN[]    = {0xBF, 0xEF, 0xBB, 0xEA, 0x3A};
static const uint8_t REGION_JEJU[]     = {0xC1, 0xA6, 0xC1, 0xD6, 0x3A};

static const uint8_t WEATHER_SUNNY[]   = {0xB8, 0xBC, 0xC0, 0xBD};
static const uint8_t WEATHER_CLOUDY[]  = {0xC8, 0xE5, 0xB8, 0xB2};
static const uint8_t WEATHER_HUMID[]   = {0xC8, 0xE5, 0xB8, 0xB2, 0x28, 0xB0, 0xED, 0xBD, 0xC0, 0xB5, 0xB5, 0x29};
static const uint8_t WEATHER_RAIN[]    = {0xBA, 0xF1};
static const uint8_t WEATHER_SNOW[]    = {0xB4, 0xAB};
static const uint8_t WEATHER_PRECIP[]  = {0xB0, 0xAD, 0xBC, 0xF6};
static const uint8_t WEATHER_CLOUD[]   = {0xB1, 0xB8, 0xB8, 0xA7};
static const uint8_t WEATHER_UNKNOWN[] = {0xB3, 0xAF, 0xBE, 0xBE, 0x20, 0xBE, 0xCB, 0x20, 0xBC, 0xF6, 0x20, 0xBE, 0xF8, 0xC0, 0xBD, 0x28, 0x41, 0x50, 0x49, 0x20, 0xC1, 0xF6, 0xBF, 0xAC, 0x29};
static const uint8_t WEATHER_FAIL[]    = {0xB3, 0xAF, 0xBE, 0xBE, 0x20, 0xC5, 0xEB, 0xBD, 0xC5, 0x20, 0xBD, 0xC7, 0xC6, 0xD0};
static const uint8_t WEATHER_ENV_ERR[] = {0xC8, 0xAF, 0xB0, 0xE6, 0x20, 0xBA, 0xAF, 0xBC, 0xF6, 0x20, 0xBF, 0xC0, 0xB7, 0xF9};
static const EncodedRegionText ENCODED_REGIONS[] = {
  {"\xEC\x84\x9C\xEC\x9A\xB8", REGION_SEOUL, sizeof(REGION_SEOUL)}, {"\xEC\x88\x98\xEC\x9B\x90", REGION_SUWON, sizeof(REGION_SUWON)},
  {"\xEC\x84\xB1\xEB\x82\xA8", REGION_SEONGNAM, sizeof(REGION_SEONGNAM)}, {"\xEC\x95\x88\xEC\x96\x91", REGION_ANYANG, sizeof(REGION_ANYANG)},
  {"\xEA\xB3\xA0\xEC\x96\x91", REGION_GOYANG, sizeof(REGION_GOYANG)}, {"\xEC\x9A\xA9\xEC\x9D\xB8", REGION_YONGIN, sizeof(REGION_YONGIN)},
  {"\xEB\xB6\x80\xEC\xB2\x9C", REGION_BUCHEON, sizeof(REGION_BUCHEON)}, {"\xEC\x95\x88\xEC\x82\xB0", REGION_ANSAN, sizeof(REGION_ANSAN)},
  {"\xEB\x82\xA8\xEC\x96\x91\xEC\xA3\xBC", REGION_NAMYANGJU, sizeof(REGION_NAMYANGJU)}, {"\xED\x99\x94\xEC\x84\xB1", REGION_HWASEONG, sizeof(REGION_HWASEONG)},
  {"\xED\x8F\x89\xED\x83\x9D", REGION_PYEONGTAEK, sizeof(REGION_PYEONGTAEK)}, {"\xEC\x9D\x98\xEC\xA0\x95\xEB\xB6\x80", REGION_UIJEONGBU, sizeof(REGION_UIJEONGBU)},
  {"\xED\x8C\x8C\xEC\xA3\xBC", REGION_PAJU, sizeof(REGION_PAJU)}, {"\xEA\xB9\x80\xED\x8F\xAC", REGION_GIMPO, sizeof(REGION_GIMPO)},
  {"\xEA\xB4\x91\xEB\xAA\x85", REGION_GWANGMYEONG, sizeof(REGION_GWANGMYEONG)}, {"\xEA\xB4\x91\xEC\xA3\xBC", REGION_GWANGJU, sizeof(REGION_GWANGJU)},
  {"\xEC\x9D\xB4\xEC\xB2\x9C", REGION_ICHEON, sizeof(REGION_ICHEON)}, {"\xEC\x96\x91\xEC\xA3\xBC", REGION_YANGJU, sizeof(REGION_YANGJU)},
  {"\xEA\xB5\xAC\xEB\xA6\xAC", REGION_GURI, sizeof(REGION_GURI)}, {"\xED\x8F\xAC\xEC\xB2\x9C", REGION_POCHEON, sizeof(REGION_POCHEON)},
  {"\xEC\x96\x91\xED\x8F\x89", REGION_YANGPYEONG, sizeof(REGION_YANGPYEONG)}, {"\xEA\xB0\x80\xED\x8F\x89", REGION_GAPYEONG, sizeof(REGION_GAPYEONG)},
  {"\xEC\x8B\x9C\xED\x9D\xA5", REGION_SIHEUNG, sizeof(REGION_SIHEUNG)}, {"\xEC\x9D\xB8\xEC\xB2\x9C", REGION_INCHEON, sizeof(REGION_INCHEON)},
  {"\xEA\xB0\x95\xEC\x9B\x90", REGION_GANGWON, sizeof(REGION_GANGWON)}, {"\xEC\xB6\xA9\xEB\xB6\x81", REGION_CHUNGBUK, sizeof(REGION_CHUNGBUK)},
  {"\xEC\xB6\xA9\xEB\x82\xA8", REGION_CHUNGNAM, sizeof(REGION_CHUNGNAM)}, {"\xEB\x8C\x80\xEC\xA0\x84", REGION_DAEJEON, sizeof(REGION_DAEJEON)},
  {"\xEC\x84\xB8\xEC\xA2\x85", REGION_SEJONG, sizeof(REGION_SEJONG)}, {"\xEC\xA0\x84\xEB\xB6\x81", REGION_JEONBUK, sizeof(REGION_JEONBUK)},
  {"\xEC\xA0\x84\xEB\x82\xA8", REGION_JEONNAM, sizeof(REGION_JEONNAM)}, {"\xEB\x8C\x80\xEA\xB5\xAC", REGION_DAEGU, sizeof(REGION_DAEGU)},
  {"\xEA\xB2\xBD\xEB\xB6\x81", REGION_GYEONGBUK, sizeof(REGION_GYEONGBUK)}, {"\xEC\x9A\xB8\xEB\xA6\x89\xEB\x8F\x84", REGION_ULLEUNGDO, sizeof(REGION_ULLEUNGDO)},
  {"\xEA\xB2\xBD\xEB\x82\xA8", REGION_GYEONGNAM, sizeof(REGION_GYEONGNAM)}, {"\xEB\xB6\x80\xEC\x82\xB0", REGION_BUSAN, sizeof(REGION_BUSAN)},
  {"\xEC\x9A\xB8\xEC\x82\xB0", REGION_ULSAN, sizeof(REGION_ULSAN)}, {"\xEC\xA0\x9C\xEC\xA3\xBC", REGION_JEJU, sizeof(REGION_JEJU)}
};

static const EncodedWeatherText ENCODED_WEATHERS[] = {
  {"\xEB\xA7\x91\xEC\x9D\x8C", WEATHER_SUNNY, sizeof(WEATHER_SUNNY)},
  {"\xED\x9D\x90\xEB\xA6\xBC", WEATHER_CLOUDY, sizeof(WEATHER_CLOUDY)},
  {"\xED\x9D\x90\xEB\xA6\xBC\x28\xEA\xB3\xA0\xEC\x8A\xB5\xEB\x8F\x84\x29", WEATHER_HUMID, sizeof(WEATHER_HUMID)},
  {"\xEB\xB9\x84", WEATHER_RAIN, sizeof(WEATHER_RAIN)}, {"\xEB\x88\x88", WEATHER_SNOW, sizeof(WEATHER_SNOW)},
  {"\xEA\xB0\x95\xEC\x88\x98", WEATHER_PRECIP, sizeof(WEATHER_PRECIP)}, {"\xEA\xB5\xAC\xEB\xA6\x84", WEATHER_CLOUD, sizeof(WEATHER_CLOUD)},
  {"\xEB\x82\xA0\xEC\x94\xA8\x20\xEC\x95\x8C\x20\xEC\x88\x98\x20\xEC\x97\x86\xEC\x9D\x8C\x28\x41\x50\x49\x20\xEC\xA7\x80\xEC\x97\xB0\x29", WEATHER_UNKNOWN, sizeof(WEATHER_UNKNOWN)},
  {"\xEB\x82\xA0\xEC\x94\xA8\x20\xED\x86\xB5\xEC\x8B\xA0\x20\xEC\x8B\xA4\xED\x8C\xA8", WEATHER_FAIL, sizeof(WEATHER_FAIL)},
  {"\xED\x99\x98\xEA\xB2\xBD\x20\xEB\xB3\x80\xEC\x88\x98\x20\xEC\x98\xA4\xEB\xA5\x98", WEATHER_ENV_ERR, sizeof(WEATHER_ENV_ERR)}
};
// =================================================================
// 🚀 서버 연동 데이터 빌드 & 송신 헬퍼
// =================================================================
static String buildDisplayModePayload(const String &activeMode, int activeScent = 0) {
  JsonDocument reqDoc; reqDoc["action"] = "DISPLAY_MODE"; reqDoc["mode"] = "display";
  reqDoc["active_mode"] = activeMode;
  if (activeScent > 0) reqDoc["active_scent"] = activeScent;
  String payload; serializeJson(reqDoc, payload); return payload;
}
static void syncDisplayModeToServer(const String &activeMode, int activeScent = 0) {
  if (WiFi.status() != WL_CONNECTED) return;
  sendServerRequest(buildDisplayModePayload(activeMode, activeScent));
}
static void syncLocalIntensityToServer() {
  JsonDocument doc; doc["action"] = "SET_INTENSITY"; doc["intensity"] = currentIntensity;
  String payload; serializeJson(doc, payload); sendServerRequest(payload);
}
static void syncLocalVolumeToServer(int displayLevel) {
  JsonDocument doc; doc["action"] = "SET_VOLUME"; doc["volume"] = constrain(displayLevel, VOLUME_LEVEL_MIN, VOLUME_LEVEL_MAX);
  String payload; serializeJson(doc, payload); sendServerRequest(payload);
}
static void syncLocalLedToServer() {
  JsonDocument doc; doc["action"] = "SET_LED"; doc["led_r"] = ledEnabled ? ledR : 0; doc["led_g"] = ledEnabled ? ledG : 0; doc["led_b"] = ledEnabled ? ledB : 0;
  doc["led_br"] = ledBrightness; doc["led_bright"] = ledBrightness;
  String payload; serializeJson(doc, payload); sendServerRequest(payload);
}

// =================================================================
// 🎨 Nextion 통신 기본 함수
// =================================================================
static void nexWriteTerminator() { nexSerial.write(0xFF); nexSerial.write(0xFF); nexSerial.write(0xFF); }
void clearNextionInputBuffer() { while (nexSerial.available()) nexSerial.read(); }
void nexSend(const String &cmd) { nexSerial.print(cmd); nexWriteTerminator(); }

static void sendClockDisplayNow() {
  struct tm timeinfo; if (!getLocalTime(&timeinfo)) { nexSend("t_time.txt=\"--:--\""); return; }
  char timeText[6]; snprintf(timeText, sizeof(timeText), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  nexSend("t_time.txt=\"" + String(timeText) + "\"");
}

static void scheduleWeatherFieldUpdate(bool loading) { pendingWeatherFieldUpdate = true; pendingWeatherLoadingUpdate = loading; }
void showPage(int pageId) {
  currentDisplayPage = pageId; nexSend("page page" + String(pageId));
  pendingPageUpdate = true; pageTransitionTime = millis(); transitioningPageId = pageId;
}

// =================================================================
// 💡 블렌드 모드 판단 로직
// =================================================================
int blendSinglePage(int scent) { switch (scent) { case 1: return 10; case 2: return 14; case 3: return 17; case 4: return 19; default: return PAGE_BLEND_HOME; } }
int blendPairPage(const String &selection) {
  if (selection.indexOf("1") >= 0 && selection.indexOf("2") >= 0) return 11; if (selection.indexOf("1") >= 0 && selection.indexOf("3") >= 0) return 12; if (selection.indexOf("1") >= 0 && selection.indexOf("4") >= 0) return 13;
  if (selection.indexOf("2") >= 0 && selection.indexOf("3") >= 0) return 15; if (selection.indexOf("2") >= 0 && selection.indexOf("4") >= 0) return 16; if (selection.indexOf("3") >= 0 && selection.indexOf("4") >= 0) return 18;
  return PAGE_BLEND_HOME;
}
bool isValidBlendSelection(const String &selection) {
  if (selection.length() != 2) return false;
  int first = selection.charAt(0) - '0'; int second = selection.charAt(1) - '0';
  return first >= 1 && first <= 4 && second >= 1 && second <= 4 && first != second;
}
bool isValidBlendCommand(int activeScent) { return isValidBlendSelection(String(activeScent)); }
String makeBlendPair(const String &firstScent, const String &secondScent) {
  if (firstScent.length() != 1 || secondScent.length() != 1 || firstScent == secondScent) return "";
  char first = firstScent.charAt(0); char second = secondScent.charAt(0);
  if (first < '1' || first > '4' || second < '1' || second > '4') return "";
  if (first > second) { char tmp = first; first = second; second = tmp; }
  String pair = ""; pair += first; pair += second; return pair;
}
void clearBlendSelectionOnly() { blendSelection = ""; pendingManualScent = ""; }
bool isBlendSelectionInProgress() { return blendModeEnabled && !blendSprayActive && blendSelection.length() == 1; }
bool isBlendUiActive() { return blendModeEnabled || (currentDisplayPage >= PAGE_BLEND_HOME && currentDisplayPage <= 19); }

void showManualPageForServerScent(int activeScent) {
  if (isValidBlendCommand(activeScent)) {
    blendModeEnabled = true; blendSprayActive = true; blendSelection = ""; nexSend("b_blend.txt=\"blendOFF\"");
    lastManualPage = blendPairPage(String(activeScent)); showPage(lastManualPage); return;
  }
  int firstScent = activeScent; while (firstScent > 9) firstScent /= 10;
  if (firstScent < 1 || firstScent > 4) { showManualPageByState(); return; }
  if (blendModeEnabled) { blendSprayActive = false; blendSelection = String(firstScent); lastManualPage = blendSinglePage(firstScent); } 
  else { blendSprayActive = false; blendSelection = ""; lastManualPage = PAGE_MANUAL_SCENT_BASE + firstScent; }
  showPage(lastManualPage);
}
static void exitBlendModeToManual(bool stopRunningBlend) {
  if (stopRunningBlend && blendSprayActive) { forceAllOff(); myDFPlayer.stop(); isRunning = false; isSpraying = false; for (int i = 0; i < 4; i++) activeNozzles[i] = false; }
  blendModeEnabled = false; blendSelection = ""; blendSprayActive = false; pendingManualScent = "";
  currentMode = MODE_MANUAL; nexSend("b_blend.txt=\"blendON\""); lastManualPage = PAGE_MANUAL; showPage(PAGE_MANUAL);
}

// =================================================================
// 🌦️ 날씨/화면 전환 렌더링
// =================================================================
static void setTempFont(int fontId) { nexSend("t_temp.font=" + String(fontId)); }
static void clearWeatherDisplayFields() { nexSend("t_region.txt=\"\""); nexSend("t_weather.txt=\"\""); setTempFont(TEMP_FONT_NORMAL); nexSend("t_temp.txt=\"\""); nexSend("t_humi.txt=\"\""); }
void showWeatherOffPage() {
  showPage(PAGE_WEATHER_OFF);
  clearWeatherDisplayFields();
}
static void showInitialHomePage() { if (offlineModeActive) showPage(PAGE_OFFLINE); else showWeatherOffPage(); }
void showWeatherPageByState() { showPage(PAGE_WEATHER); if (currentMode == MODE_WEATHER) scheduleWeatherFieldUpdate(false); else scheduleWeatherFieldUpdate(true); }
void clearWeatherState() { hasWeatherSnapshot = false; hasTempHumiSnapshot = false; lastWeatherLabel = ""; lastWeatherIconId = 0; }
static void showHomePageByWeatherState() { if (currentMode == MODE_WEATHER) showWeatherPageByState(); else showWeatherOffPage(); }
void showStartupReadyPage() { nexSend("sleep=0"); nexSend("dim=100"); clearWeatherState(); setSystemMode(MODE_READY, "Startup Ready"); showInitialHomePage(); }
void setOfflineModeActive(bool active) { offlineModeActive = active; if (offlineModeActive) { if (currentMode == MODE_WEATHER) { clearWeatherState(); setSystemMode(MODE_READY, "Offline Mode"); } showPage(PAGE_OFFLINE); } else { showWeatherOffPage(); } }
void showLowFluidPage(int cartNum) {
  int returnPage = currentDisplayPage; showPage(PAGE_LOW_FLUID);
  if (cartNum >= 1 && cartNum <= 4) {
    uint8_t encoded[8] = { (uint8_t)('0' + cartNum), 0xB9, 0xF8, 0x20, 0xBA, 0xCE, 0xC1, 0xB7 };
    nexSerial.print("t_capnum.txt=\""); nexSerial.write(encoded, 8); nexSerial.print("\""); nexWriteTerminator();
  }
  delay(3000); if (returnPage == PAGE_LOW_FLUID) showHomePageByWeatherState(); else showPage(returnPage);
}

// =================================================================
// 🔡 한글 인코딩 변환 처리
// =================================================================
static bool sendEncodedRegionText(const String &regionText) {
  for (size_t i = 0; i < sizeof(ENCODED_REGIONS) / sizeof(ENCODED_REGIONS[0]); i++) {
    if (regionText == ENCODED_REGIONS[i].utf8) { nexSerial.print("t_region.txt=\""); nexSerial.write(ENCODED_REGIONS[i].encoded, ENCODED_REGIONS[i].length); nexSerial.print("\""); nexWriteTerminator(); return true; }
  } return false;
}
static bool sendEncodedWeatherBytes(const uint8_t *encoded, size_t length) { nexSerial.print("t_weather.txt=\""); nexSerial.write(encoded, length); nexSerial.print("\""); nexWriteTerminator(); return true; }
static bool sendEncodedWeatherText(const String &weatherText) {
  for (size_t i = 0; i < sizeof(ENCODED_WEATHERS) / sizeof(ENCODED_WEATHERS[0]); i++) { if (weatherText == ENCODED_WEATHERS[i].utf8) return sendEncodedWeatherBytes(ENCODED_WEATHERS[i].encoded, ENCODED_WEATHERS[i].length); }
  if (weatherText.indexOf("\xEB\xB9\x84") >= 0) return sendEncodedWeatherBytes(WEATHER_RAIN, sizeof(WEATHER_RAIN)); if (weatherText.indexOf("\xEA\xB0\x95\xEC\x88\x98") >= 0) return sendEncodedWeatherBytes(WEATHER_PRECIP, sizeof(WEATHER_PRECIP));
  if (weatherText.indexOf("\xEB\x88\x88") >= 0) return sendEncodedWeatherBytes(WEATHER_SNOW, sizeof(WEATHER_SNOW)); if (weatherText.indexOf("\xED\x9D\x90\xEB\xA6\xBC") >= 0) return sendEncodedWeatherBytes(WEATHER_CLOUDY, sizeof(WEATHER_CLOUDY));
  if (weatherText.indexOf("\xEA\xB5\xAC\xEB\xA6\x84") >= 0) return sendEncodedWeatherBytes(WEATHER_CLOUD, sizeof(WEATHER_CLOUD)); if (weatherText.indexOf("\xEB\xA7\x91") >= 0) return sendEncodedWeatherBytes(WEATHER_SUNNY, sizeof(WEATHER_SUNNY));
  if (weatherText.indexOf("\xEC\x95\x8C\x20\xEC\x88\x98\x20\xEC\x97\x86\xEC\x9D\x8C") >= 0) return sendEncodedWeatherBytes(WEATHER_UNKNOWN, sizeof(WEATHER_UNKNOWN));
  if (weatherText.indexOf("\xED\x86\xB5\xEC\x8B\xA0\x20\xEC\x8B\xA4\xED\x8C\xA8") >= 0) return sendEncodedWeatherBytes(WEATHER_FAIL, sizeof(WEATHER_FAIL));
  if (weatherText.indexOf("\xEC\x98\xA4\xEB\xA5\x98") >= 0) return sendEncodedWeatherBytes(WEATHER_ENV_ERR, sizeof(WEATHER_ENV_ERR));
  return false;
}
int weatherIconFromText(const String &weatherText) {
  String normalized = weatherText; normalized.trim();
  if (normalized.indexOf("\xEB\xB9\x84") >= 0 || normalized.indexOf("\xEA\xB0\x95\xEC\x88\x98") >= 0) return 3; if (normalized.indexOf("\xEB\x88\x88") >= 0) return 4;
  if (normalized.indexOf("\xED\x9D\x90\xEB\xA6\xBC") >= 0 || normalized.indexOf("\xEA\xB5\xAC\xEB\xA6\x84") >= 0) return 2; if (normalized.indexOf("\xEB\xA7\x91") >= 0 || normalized.indexOf("\xED\x95\xB4") >= 0) return 1; return 0;
}
bool hasValidWeatherText(const String &weatherText) { String normalized = weatherText; normalized.trim(); return normalized.length() > 0 && normalized != "Loading" && normalized != "Weather"; }

// =================================================================
// ☁️ 날씨 정보 갱신 루틴
// =================================================================
static void sendCachedTempHumi() {
  setTempFont(TEMP_FONT_NORMAL); if (!hasTempHumiSnapshot) { nexSend("t_temp.txt=\"\""); nexSend("t_humi.txt=\"\""); return; }
  nexSend("t_temp.txt=\"" + String(lastWeatherTempC, 1) + "C\""); nexSend("t_humi.txt=\"" + String(lastWeatherHumi, 0) + "%\"");
}
static void showWeatherLoadingFields() { nexSend("t_region.txt=\"\""); nexSend("t_weather.txt=\"\""); setTempFont(TEMP_FONT_LOADING); nexSend("t_temp.txt=\"Loading\""); nexSend("t_humi.txt=\"\""); }
static void updateWeatherFields();
static void refreshWeatherFieldsOnVisiblePage(bool loading) {
  if (currentDisplayPage != PAGE_WEATHER) return;
  if (!loading && currentMode != MODE_WEATHER) return;
  if (pendingPageUpdate) { scheduleWeatherFieldUpdate(loading); return; }
  if (loading) showWeatherLoadingFields(); else updateWeatherFields();
}
void beginWeatherRefresh() { hasWeatherSnapshot = false; hasTempHumiSnapshot = false; lastWeatherLabel = ""; lastWeatherIconId = 0; refreshWeatherFieldsOnVisiblePage(true); }
static void updateWeatherFields() {
  String regionText = lastWeatherRegion; regionText.trim(); String weatherText = lastWeatherLabel; weatherText.trim();
  if (!hasWeatherSnapshot || !hasValidWeatherText(weatherText)) { showWeatherLoadingFields(); return; }
  if (regionText.length() == 0) nexSend("t_region.txt=\"\""); else if (!sendEncodedRegionText(regionText)) nexSend("t_region.txt=\"\"");
  if (!sendEncodedWeatherText(weatherText)) sendEncodedWeatherBytes(WEATHER_UNKNOWN, sizeof(WEATHER_UNKNOWN)); sendCachedTempHumi();
}
void refreshWeatherFieldsIfVisible() { refreshWeatherFieldsOnVisiblePage(false); }
void showWeatherPageForRefreshResponse(bool isWeatherRefreshResponse) {
  if (!isWeatherRefreshResponse || currentMode != MODE_WEATHER) return;
  refreshWeatherFieldsOnVisiblePage(false);
}

// =================================================================
// 🕹️ Nextion 디스플레이 명령어 입력 파싱 및 라우팅
// =================================================================
static String findNextionCmd(String buf) {
  buf.trim(); if (buf.length() != 2) return "";
  const char* tokens[] = { "MM", "MO", "MD", "DS", "IM", "WR", "LN", "LO", "L0", "LF", "L1", "L2", "L3", "M1", "M2", "M3", "Y1", "Y2", "Y3", "I1", "I2", "I3", "IH", "VU", "VD", "VM", "ST", "BM", "RE", "OF", "TZ", "WK", "S1", "S2", "S3", "S4" };
  for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++) { if (buf.indexOf(tokens[i]) >= 0) return String(tokens[i]); } return "";
}
String processNextionChar(char c, String &buf, bool executeCmd) {
  if (isalnum((unsigned char)c) || c == '_' || c == ' ') buf += c; else { buf = ""; return ""; }
  String cmd = findNextionCmd(buf); if (cmd.length() > 0) { if (executeCmd) handleNextionCmd(cmd); buf = ""; return cmd; } if (buf.length() > 64) buf = ""; return "";
}
void checkNextionInput() {
  static String buf = ""; unsigned long startMillis = millis();
  while (nexSerial.available()) {
    char c = (char)nexSerial.read(); if ((c >= 'A' && c <= 'Z') || c == 0x65 || c == 0x67 || c == 0x68) lastActivityTime = millis();
    String cmd = processNextionChar(c, buf, false); if (cmd.length() == 0) { if (millis() - startMillis > 50) break; continue; }
    if (cmd == "WK") { wakeUpSystem(); if (millis() - startMillis > 50) break; continue; }
    handleNextionCmd(cmd); wakeUpSystem(); if (millis() - startMillis > 50) break;
  }
  if (pendingManualScent.length() > 0 && millis() - lastScentTouchTime > 300) {
    if (isBlendSelectionInProgress() && pendingManualScent.length() < 2) { pendingManualScent = ""; return; }
    String scentToRun = pendingManualScent; runManualMode(scentToRun); if (blendModeEnabled) blendSprayActive = scentToRun.length() >= 2; pendingManualScent = "";
  }
}

static void toggleBlendMode() {
  if (blendModeEnabled || (currentDisplayPage >= PAGE_BLEND_HOME && currentDisplayPage <= 19)) { exitBlendModeToManual(true); Serial.println("\r\n[BLEND] OFF"); return; }
  blendModeEnabled = true; blendSelection = ""; blendSprayActive = false; pendingManualScent = ""; nexSend("b_blend.txt=\"blendOFF\""); lastManualPage = PAGE_BLEND_HOME; showPage(PAGE_BLEND_HOME); Serial.println("\r\n[BLEND] ON");
}
static void handleScentButton(const String &cmd) {
  int scent = cmd.substring(1).toInt(); if (scent < 1 || scent > 4) return;
  Serial.printf("\r\n[S BUTTON] %s\r\n", cmd.c_str()); String scentStr = String(scent);
  if (!isBlendUiActive()) { currentMode = MODE_MANUAL; clearBlendSelectionOnly(); blendSprayActive = false; lastManualPage = PAGE_MANUAL_SCENT_BASE + scent; showPage(lastManualPage); syncDisplayModeToServer("manual", scent); pendingManualScent = String(scent); lastScentTouchTime = millis(); return; }
  blendModeEnabled = true;
  if (blendSprayActive) { if (isRunning) stopSystem(); blendModeEnabled = true; blendSprayActive = false; pendingManualScent = ""; nexSend("b_blend.txt=\"blendOFF\""); blendSelection = scentStr; lastManualPage = blendSinglePage(scent); showPage(lastManualPage); return; }
  if (blendSelection.length() == 0 || blendSelection == scentStr) { blendSelection = scentStr; lastManualPage = blendSinglePage(scent); showPage(lastManualPage); return; }
  String pair = makeBlendPair(blendSelection, scentStr);
  if (!isValidBlendSelection(pair)) { blendSelection = scentStr; lastManualPage = blendSinglePage(scent); showPage(lastManualPage); return; }
  lastManualPage = blendPairPage(pair); showPage(lastManualPage); currentMode = MODE_MANUAL; syncDisplayModeToServer("manual", pair.toInt()); pendingManualScent = pair; lastScentTouchTime = millis(); blendSelection = ""; blendSprayActive = true;
}

static int currentIntensityPage() { if (currentIntensity == 1) return PAGE_INTENSITY_LOW; if (currentIntensity == 2) return PAGE_INTENSITY_MEDIUM; if (currentIntensity == 3) return PAGE_INTENSITY_HIGH; return PAGE_INTENSITY_HOME; }
static void handleIntensityButton(const String &cmd) {
  int intensity = (cmd == "IH") ? 3 : cmd.substring(1).toInt(); if (intensity < 1 || intensity > 3) return;
  SprayIntensity(intensity); markLocalSettingsChanged(); syncLocalIntensityToServer(); showPage(currentIntensityPage());
  const char *label = (currentIntensity == 1) ? "Low" : (currentIntensity == 2) ? "Medium" : "High"; updateDisplay(0, String("Intensity ") + label); Serial.printf("\r\n[Display] Intensity: %d\r\n", currentIntensity);
}

static int getDisplayVolumeLevel() { return constrain((currentVolume + 1) / 3, 0, 10); }
static void showDisplayVolumePage(int level) { int displayLevel = constrain(level, VOLUME_LEVEL_MIN, VOLUME_LEVEL_MAX); showPage(VOLUME_PAGE_BASE + displayLevel); }
static void setDisplayVolumeLevel(int level) { int displayLevel = constrain(level, 0, 10); changeVolume(displayLevel * 3); markLocalSettingsChanged(); syncLocalVolumeToServer(displayLevel); showDisplayVolumePage(displayLevel); }
void applyServerVolumeWithoutPageChange(int volumeValue) { int scaledVol = (volumeValue <= VOLUME_LEVEL_MAX) ? (volumeValue * 3) : volumeValue; scaledVol = constrain(scaledVol, 0, 30); if (scaledVol != currentVolume) changeVolume(scaledVol); }
static void handleVolumeButton(const String &cmd) { if (cmd == "VU") setDisplayVolumeLevel(getDisplayVolumeLevel() + 1); else if (cmd == "VD") setDisplayVolumeLevel(getDisplayVolumeLevel() - 1); else if (cmd == "VM") showDisplayVolumePage(getDisplayVolumeLevel()); }

static int currentLedPage() { if (!ledEnabled) return PAGE_LED_OFF; if (ledBrightness <= 90) return PAGE_LED_DIM; if (ledBrightness >= 220) return PAGE_LED_BRIGHT; return PAGE_LED_NORMAL; }
static void showCurrentLedPage() { showPage(currentLedPage()); }
static bool isLedColorOff(uint8_t r, uint8_t g, uint8_t b) { return r == 0 && g == 0 && b == 0; }
static void ensureVisibleLedColor() { if (!isLedColorOff(ledR, ledG, ledB)) return; ledR = 255; ledG = 255; ledB = 255; prefs.putUChar("ledR", ledR); prefs.putUChar("ledG", ledG); prefs.putUChar("ledB", ledB); }
static void persistLocalLedState() { prefs.putBool("ledEnabled", ledEnabled); prefs.putInt("ledBright", ledBrightness); prefs.putUChar("ledR", ledR); prefs.putUChar("ledG", ledG); prefs.putUChar("ledB", ledB); }
static void setDisplayLedEnabled(bool enabled) { ledEnabled = enabled; if (ledEnabled && ledBrightness <= 0) ledBrightness = 150; if (ledEnabled) ensureVisibleLedColor(); persistLocalLedState(); markLocalSettingsChanged(); syncLocalLedToServer(); showPage(ledEnabled ? currentLedPage() : PAGE_LED_OFF); setLedColor(ledEnabled ? ledR : 0, ledEnabled ? ledG : 0, ledEnabled ? ledB : 0); }
static void setDisplayLedBrightness(int brightness, int pageId) { ledEnabled = true; ledBrightness = constrain(brightness, 0, 255); ensureVisibleLedColor(); persistLocalLedState(); markLocalSettingsChanged(); syncLocalLedToServer(); showPage(pageId); setLedColor(ledR, ledG, ledB); }
static void handleLedButton(const String &cmd) { if (cmd == "LO" || cmd == "L0") setDisplayLedEnabled(true); else if (cmd == "LF") setDisplayLedEnabled(false); else if (cmd == "L1") setDisplayLedBrightness(60, PAGE_LED_DIM); else if (cmd == "L2") setDisplayLedBrightness(150, PAGE_LED_NORMAL); else if (cmd == "L3") setDisplayLedBrightness(255, PAGE_LED_BRIGHT); }

void showManualPageByState() { if (blendModeEnabled) showPage((currentDisplayPage >= PAGE_BLEND_HOME && currentDisplayPage <= 19) ? lastManualPage : PAGE_BLEND_HOME); else showPage((lastManualPage >= PAGE_MANUAL_SCENT_BASE + 1 && lastManualPage <= PAGE_MANUAL_SCENT_BASE + 4) ? lastManualPage : PAGE_MANUAL); }

void handleNextionCmd(const String &cmd) {
  if (cmd.startsWith("p0") || cmd.startsWith("t_weather") || cmd.startsWith("j0")) return;
  static unsigned long lastValidCmdMillis = 0; static String lastScentCmd = ""; static unsigned long lastScentCmdMillis = 0;
  bool isVolumeCmd = cmd.startsWith("V"); bool isScentCmd = cmd.startsWith("S"); bool isBlendScentCmd = isScentCmd && isBlendUiActive();
  unsigned long cooldown = isVolumeCmd ? 50 : 1000;
  bool isNoCooldownCmd = (isBlendScentCmd || cmd == "ST" || cmd == "WR" || cmd == "BM" || cmd == "DS" || cmd == "OF" || cmd.startsWith("M") || cmd.startsWith("Y"));

  if (!isNoCooldownCmd && millis() - lastValidCmdMillis < cooldown) return;
  lastValidCmdMillis = millis();
  if (isBlendScentCmd) { if (cmd == lastScentCmd && millis() - lastScentCmdMillis < 250) return; lastScentCmd = cmd; lastScentCmdMillis = millis(); }
  Serial.printf("\r\n[Nextion] CMD: %s\r\n", cmd.c_str());

  if (cmd == "MM" || cmd == "MO" || cmd == "MD") showPage(PAGE_MODE_SELECT);
  else if (cmd == "OF") setOfflineModeActive(!offlineModeActive);
  else if (cmd == "DS") { showPage(PAGE_DEVICE_STATUS); updateScentProgressBars(); }
  else if (cmd == "IM") showPage(currentIntensityPage());
  else if (cmd == "WR") { prefs.putInt("wifi_return_page", currentDisplayPage); showPage(PAGE_WIFI_RESET); }
  else if (cmd == "M2") {
    if (offlineModeActive) showWeatherOffPage();
    else if (currentMode == MODE_WEATHER && currentDisplayPage == PAGE_WEATHER) { markLocalStop(); clearWeatherState(); setSystemMode(MODE_READY, "Weather Mode Off"); syncDisplayModeToServer("ready"); showWeatherOffPage(); }
    else if (currentMode == MODE_WEATHER) showWeatherPageByState();
    else showWeatherOffPage();
  }
  else if (cmd == "Y3") { if (offlineModeActive) showPage(PAGE_OFFLINE); else if (currentMode == MODE_WEATHER) { clearLocalStopMark(); markLocalWeatherStart(); showPage(PAGE_WEATHER); beginWeatherRefresh(); syncDisplayModeToServer("weather"); requestWeatherRefresh(lastWeatherRegion); } else { clearLocalStopMark(); markLocalWeatherStart(); showPage(PAGE_WEATHER); beginWeatherRefresh(); enterWeatherMode(false); syncDisplayModeToServer("weather"); requestWeatherRefresh(lastWeatherRegion); } }
  else if (cmd == "M1" || cmd == "Y1") { showManualPageByState(); if (currentMode != MODE_WEATHER) setSystemMode(MODE_MANUAL, "Manual Mode"); } 
  else if (cmd == "M3" || cmd == "Y2") { showPage(PAGE_DEVICE_STATUS); updateScentProgressBars(); if (currentMode != MODE_WEATHER) setSystemMode(MODE_SETTING, "Setting Mode"); } 
  else if (cmd == "ST") {
    SystemMode modeBeforeStop = currentMode; lastManualPage = PAGE_MANUAL; markLocalStop(); setSystemMode(MODE_READY, "Stopped"); pendingManualScent = "";
    if (modeBeforeStop == MODE_WEATHER) { hasWeatherSnapshot = false; hasTempHumiSnapshot = false; lastWeatherLabel = ""; lastWeatherIconId = 0; syncDisplayModeToServer("ready"); showWeatherOffPage(); } 
    else if (modeBeforeStop == MODE_MANUAL) { lastStoppedManualScent = (currentDisplayPage >= PAGE_MANUAL_SCENT_BASE + 1 && currentDisplayPage <= PAGE_MANUAL_SCENT_BASE + 4) ? (currentDisplayPage - PAGE_MANUAL_SCENT_BASE) : 0; manualModeOffMillis = millis(); syncDisplayModeToServer("ready"); showPage(PAGE_MANUAL); } 
    else showPage(PAGE_MODE_SELECT);
  } 
  else if (cmd == "BM") toggleBlendMode(); 
  else if (cmd.startsWith("I")) handleIntensityButton(cmd); else if (cmd.startsWith("V")) handleVolumeButton(cmd);
  else if (cmd.startsWith("L")) { if (cmd == "LN") showCurrentLedPage(); else handleLedButton(cmd); }
  else if (cmd.startsWith("S")) handleScentButton(cmd); 
  else if (cmd == "RE") { Serial.println("Wi-Fi 초기화 및 재부팅!"); prefs.putInt("wifi_return_page", currentDisplayPage); prefs.putBool("force_config_portal", true); prefs.putBool("force_offline", false); showPage(PAGE_WIFI_RESET); WiFiManager wifiManager; wifiManager.resetSettings(); WiFi.disconnect(true, true); WiFi.mode(WIFI_OFF); delay(700); ESP.restart(); } 
  else if (cmd == "TZ") { int returnPage = currentDisplayPage; showPage(PAGE_TARE); resetScaleZero(); delay(1000); showPage(returnPage == PAGE_TARE ? PAGE_DEVICE_STATUS : returnPage); if (returnPage == PAGE_DEVICE_STATUS) updateScentProgressBars(); }
}

void updateDisplay(int iconID, String text) { 
  if (currentMode == MODE_WEATHER && currentDisplayPage == PAGE_WEATHER && lastWeatherIconId >= 1 && lastWeatherIconId <= 4) iconID = lastWeatherIconId;
  nexSend("p0.pic=" + String(iconID));
  refreshWeatherFieldsOnVisiblePage(false);
}

// =================================================================
// 🕒 디스플레이 데이터 자동 갱신 및 터미널 출력
// =================================================================
void updateClockDisplay() {
  if (pendingPageUpdate && millis() - pageTransitionTime >= 80) {
    pendingPageUpdate = false; sendClockDisplayNow();
    if (transitioningPageId == PAGE_MANUAL || transitioningPageId == PAGE_DEVICE_STATUS || (transitioningPageId >= PAGE_MANUAL_SCENT_BASE + 1 && transitioningPageId <= PAGE_MANUAL_SCENT_BASE + 4) || (transitioningPageId >= PAGE_BLEND_HOME && transitioningPageId <= 19)) updateScentProgressBars();
    if (transitioningPageId == PAGE_WEATHER && pendingWeatherFieldUpdate) { pendingWeatherFieldUpdate = false; if (pendingWeatherLoadingUpdate) showWeatherLoadingFields(); else updateWeatherFields(); } else if (transitioningPageId != PAGE_WEATHER) pendingWeatherFieldUpdate = false;
  }
  if (volumeNeedsSave && millis() - lastVolumeChangeTime > 3000) { prefs.putInt("volume", currentVolume); volumeNeedsSave = false; Serial.println(C_CYAN "[System] 볼륨 자동 저장 완료" C_RESET); }
  static unsigned long lastClockUpdate = 0;
  if (millis() - lastClockUpdate < 1000) return; lastClockUpdate = millis(); sendClockDisplayNow();
}

void updateTempHumi(float tempC, float humi) {
  lastWeatherTempC = tempC; lastWeatherHumi = humi; hasTempHumiSnapshot = true;
  refreshWeatherFieldsOnVisiblePage(false);
}

void updateProgressBar(int val) { updateProgressBar(1, val); }
void updateProgressBar(int barIndex, int val) { int safeIndex = constrain(barIndex, 1, 4); nexSend("j" + String(safeIndex) + ".val=" + String(constrain(val, 0, 100))); }

float calculateScentPercent(float weightValue) {
  const float emptyWeight = Config::EMPTY_WEIGHT; const float fallbackFullWeight = 95.4f;
  const float fullWeight = (maxWeight > emptyWeight) ? maxWeight : fallbackFullWeight;
  const float usableWeight = fullWeight - emptyWeight;
  if (usableWeight <= 0.0f) return 0.0f;
  float clampedWeight = (weightValue < emptyWeight) ? emptyWeight : weightValue;
  return constrain(((clampedWeight - emptyWeight) / usableWeight) * 100.0f, 0.0f, 100.0f);
}

void updateScentProgressBars() {
  for (int i = 0; i < 4; i++) {
    int percent = constrain((int)round(calculateScentPercent(weights[i])), 0, 100);
    updateProgressBar(i + 1, percent); nexSend("t_percent" + String(i + 1) + ".txt=\"" + String(percent) + "%\"");
  }
}

void changeVolume(int vol) { currentVolume = constrain(vol, 0, 30); myDFPlayer.volume(currentVolume); delay(40); volumeNeedsSave = true; lastVolumeChangeTime = millis(); Serial.printf("\r\033[K" C_GREEN "🔊 볼륨: %d\r\n" C_RESET, currentVolume); showPrompt(); }
void showPrompt() { Serial.print("\r\033[K" C_YELLOW "👉 명령 입력 >>" C_RESET); }
void printMainMenu() { Serial.println("\r\n[1]수동 [2]날씨 [3]설정 [4]데모 [5]음성 [6]파형 [7]반응 [8]정보 [9]LED [10]소음"); showPrompt(); }
void redrawInputLine(String &buffer) { Serial.print("\r\033[K" C_YELLOW "👉 명령 입력 >>" C_RESET); Serial.print(buffer); }
void printDashboard() { Serial.printf("\r\nWiFi: %d dBm | Weights: %.1f %.1f %.1f %.1f\n", WiFi.RSSI(), weights[0], weights[1], weights[2], weights[3]); printMainMenu(); }
