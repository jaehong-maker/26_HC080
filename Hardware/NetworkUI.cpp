#include "Globals.h"

static WiFiClientSecure sharedClient;
static bool isSessionInit = false;
static bool hasWeatherSnapshot = false;
static bool hasTempHumiSnapshot = false;
static float lastWeatherTempC = 0.0f;
static float lastWeatherHumi = 0.0f;

static const int VOLUME_PAGE_BASE = 58;
static const int VOLUME_LEVEL_MIN = 0;
static const int VOLUME_LEVEL_MAX = 10;
static const int TEMP_FONT_NORMAL = 0;
static const int TEMP_FONT_LOADING = 7;

static const int PAGE_WEATHER_OFF = 0;
static const int PAGE_MODE_SELECT = 1;
static const int PAGE_WEATHER = 2;
static const int PAGE_MANUAL = 3;
static const int PAGE_MANUAL_SCENT_BASE = 4; // scent 1~4 -> page5~8
static const int PAGE_BLEND_HOME = 9;
static const int PAGE_WIFI_RESET = 20;
static const int PAGE_DEVICE_STATUS = 21;
static const int PAGE_TARE = 22;
static const int PAGE_LED_ON = 23;
static const int PAGE_LED_OFF = 24;
static const int PAGE_LED_DIM = 25;
static const int PAGE_LED_NORMAL = 26;
static const int PAGE_LED_BRIGHT = 27;
static const int PAGE_INTENSITY_HOME = 28;
static const int PAGE_OFFLINE = 29;
static const int PAGE_INTENSITY_LOW = 30;
static const int PAGE_INTENSITY_MEDIUM = 31;
static const int PAGE_INTENSITY_HIGH = 32;
static const int PAGE_STARTUP_HOME = 33;
static const int PAGE_LOW_FLUID = 80;

static int lastManualPage = PAGE_MANUAL;
static int currentDisplayPage = PAGE_WEATHER_OFF;
static bool offlineModeActive = false;
static unsigned long lastLocalSettingsMillis = 0;
static const unsigned long LOCAL_SETTINGS_PROTECT_MS = 30000;

static String pendingManualScent = "";
static unsigned long lastScentTouchTime = 0;
static unsigned long manualModeOffMillis = 0;
static int lastStoppedManualScent = 0;

struct EncodedRegionText {
  const char *utf8;
  const uint8_t *encoded;
  size_t length;
};

struct EncodedWeatherText {
  const char *utf8;
  const uint8_t *encoded;
  size_t length;
};

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
  {"\xEC\x84\x9C\xEC\x9A\xB8", REGION_SEOUL, sizeof(REGION_SEOUL)},
  {"\xEC\x88\x98\xEC\x9B\x90", REGION_SUWON, sizeof(REGION_SUWON)},
  {"\xEC\x84\xB1\xEB\x82\xA8", REGION_SEONGNAM, sizeof(REGION_SEONGNAM)},
  {"\xEC\x95\x88\xEC\x96\x91", REGION_ANYANG, sizeof(REGION_ANYANG)},
  {"\xEA\xB3\xA0\xEC\x96\x91", REGION_GOYANG, sizeof(REGION_GOYANG)},
  {"\xEC\x9A\xA9\xEC\x9D\xB8", REGION_YONGIN, sizeof(REGION_YONGIN)},
  {"\xEB\xB6\x80\xEC\xB2\x9C", REGION_BUCHEON, sizeof(REGION_BUCHEON)},
  {"\xEC\x95\x88\xEC\x82\xB0", REGION_ANSAN, sizeof(REGION_ANSAN)},
  {"\xEB\x82\xA8\xEC\x96\x91\xEC\xA3\xBC", REGION_NAMYANGJU, sizeof(REGION_NAMYANGJU)},
  {"\xED\x99\x94\xEC\x84\xB1", REGION_HWASEONG, sizeof(REGION_HWASEONG)},
  {"\xED\x8F\x89\xED\x83\x9D", REGION_PYEONGTAEK, sizeof(REGION_PYEONGTAEK)},
  {"\xEC\x9D\x98\xEC\xA0\x95\xEB\xB6\x80", REGION_UIJEONGBU, sizeof(REGION_UIJEONGBU)},
  {"\xED\x8C\x8C\xEC\xA3\xBC", REGION_PAJU, sizeof(REGION_PAJU)},
  {"\xEA\xB9\x80\xED\x8F\xAC", REGION_GIMPO, sizeof(REGION_GIMPO)},
  {"\xEA\xB4\x91\xEB\xAA\x85", REGION_GWANGMYEONG, sizeof(REGION_GWANGMYEONG)},
  {"\xEA\xB4\x91\xEC\xA3\xBC", REGION_GWANGJU, sizeof(REGION_GWANGJU)},
  {"\xEC\x9D\xB4\xEC\xB2\x9C", REGION_ICHEON, sizeof(REGION_ICHEON)},
  {"\xEC\x96\x91\xEC\xA3\xBC", REGION_YANGJU, sizeof(REGION_YANGJU)},
  {"\xEA\xB5\xAC\xEB\xA6\xAC", REGION_GURI, sizeof(REGION_GURI)},
  {"\xED\x8F\xAC\xEC\xB2\x9C", REGION_POCHEON, sizeof(REGION_POCHEON)},
  {"\xEC\x96\x91\xED\x8F\x89", REGION_YANGPYEONG, sizeof(REGION_YANGPYEONG)},
  {"\xEA\xB0\x80\xED\x8F\x89", REGION_GAPYEONG, sizeof(REGION_GAPYEONG)},
  {"\xEC\x8B\x9C\xED\x9D\xA5", REGION_SIHEUNG, sizeof(REGION_SIHEUNG)},
  {"\xEC\x9D\xB8\xEC\xB2\x9C", REGION_INCHEON, sizeof(REGION_INCHEON)},
  {"\xEA\xB0\x95\xEC\x9B\x90", REGION_GANGWON, sizeof(REGION_GANGWON)},
  {"\xEC\xB6\xA9\xEB\xB6\x81", REGION_CHUNGBUK, sizeof(REGION_CHUNGBUK)},
  {"\xEC\xB6\xA9\xEB\x82\xA8", REGION_CHUNGNAM, sizeof(REGION_CHUNGNAM)},
  {"\xEB\x8C\x80\xEC\xA0\x84", REGION_DAEJEON, sizeof(REGION_DAEJEON)},
  {"\xEC\x84\xB8\xEC\xA2\x85", REGION_SEJONG, sizeof(REGION_SEJONG)},
  {"\xEC\xA0\x84\xEB\xB6\x81", REGION_JEONBUK, sizeof(REGION_JEONBUK)},
  {"\xEC\xA0\x84\xEB\x82\xA8", REGION_JEONNAM, sizeof(REGION_JEONNAM)},
  {"\xEB\x8C\x80\xEA\xB5\xAC", REGION_DAEGU, sizeof(REGION_DAEGU)},
  {"\xEA\xB2\xBD\xEB\xB6\x81", REGION_GYEONGBUK, sizeof(REGION_GYEONGBUK)},
  {"\xEC\x9A\xB8\xEB\xA6\x89\xEB\x8F\x84", REGION_ULLEUNGDO, sizeof(REGION_ULLEUNGDO)},
  {"\xEA\xB2\xBD\xEB\x82\xA8", REGION_GYEONGNAM, sizeof(REGION_GYEONGNAM)},
  {"\xEB\xB6\x80\xEC\x82\xB0", REGION_BUSAN, sizeof(REGION_BUSAN)},
  {"\xEC\x9A\xB8\xEC\x82\xB0", REGION_ULSAN, sizeof(REGION_ULSAN)},
  {"\xEC\xA0\x9C\xEC\xA3\xBC", REGION_JEJU, sizeof(REGION_JEJU)}
};

static const EncodedWeatherText ENCODED_WEATHERS[] = {
  {"\xEB\xA7\x91\xEC\x9D\x8C", WEATHER_SUNNY, sizeof(WEATHER_SUNNY)},
  {"\xED\x9D\x90\xEB\xA6\xBC", WEATHER_CLOUDY, sizeof(WEATHER_CLOUDY)},
  {"\xED\x9D\x90\xEB\xA6\xBC\x28\xEA\xB3\xA0\xEC\x8A\xB5\xEB\x8F\x84\x29", WEATHER_HUMID, sizeof(WEATHER_HUMID)},
  {"\xEB\xB9\x84", WEATHER_RAIN, sizeof(WEATHER_RAIN)},
  {"\xEB\x88\x88", WEATHER_SNOW, sizeof(WEATHER_SNOW)},
  {"\xEA\xB0\x95\xEC\x88\x98", WEATHER_PRECIP, sizeof(WEATHER_PRECIP)},
  {"\xEA\xB5\xAC\xEB\xA6\x84", WEATHER_CLOUD, sizeof(WEATHER_CLOUD)},
  {"\xEB\x82\xA0\xEC\x94\xA8\x20\xEC\x95\x8C\x20\xEC\x88\x98\x20\xEC\x97\x86\xEC\x9D\x8C\x28\x41\x50\x49\x20\xEC\xA7\x80\xEC\x97\xB0\x29", WEATHER_UNKNOWN, sizeof(WEATHER_UNKNOWN)},
  {"\xEB\x82\xA0\xEC\x94\xA8\x20\xED\x86\xB5\xEC\x8B\xA0\x20\xEC\x8B\xA4\xED\x8C\xA8", WEATHER_FAIL, sizeof(WEATHER_FAIL)},
  {"\xED\x99\x98\xEA\xB2\xBD\x20\xEB\xB3\x80\xEC\x88\x98\x20\xEC\x98\xA4\xEB\xA5\x98", WEATHER_ENV_ERR, sizeof(WEATHER_ENV_ERR)}
};

static String sanitizeNextionText(String text) {
  text.replace("\"", "'");
  return text;
}

static void nexWriteTerminator() {
  nexSerial.write(0xFF);
  nexSerial.write(0xFF);
  nexSerial.write(0xFF);
}

void clearNextionInputBuffer() {
  while (nexSerial.available()) {
    nexSerial.read();
  }
}

static void sendClockDisplayNow() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    nexSend("t_time.txt=\"--:--\"");
    return;
  }

  char timeText[6];
  snprintf(timeText, sizeof(timeText), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  nexSend("t_time.txt=\"" + String(timeText) + "\"");
}

static bool isManualScentPage(int pageId);
static bool isBlendPage(int pageId);

static bool pendingPageUpdate = false;
static unsigned long pageTransitionTime = 0;
static int transitioningPageId = 0;

static void showPage(int pageId) {
  currentDisplayPage = pageId;
  nexSend("page page" + String(pageId));
  
  pendingPageUpdate = true;
  pageTransitionTime = millis();
  transitioningPageId = pageId;
}

static void markLocalSettingsChanged() {
  lastLocalSettingsMillis = millis();
}

static bool canApplyServerSettings() {
  return lastLocalSettingsMillis == 0 ||
         (millis() - lastLocalSettingsMillis >= LOCAL_SETTINGS_PROTECT_MS);
}

static void syncLocalIntensityToServer() {
  JsonDocument doc;
  doc["action"] = "SET_INTENSITY";
  doc["intensity"] = currentIntensity;

  String payload;
  serializeJson(doc, payload);
  sendServerRequest(payload);
}

static void syncLocalVolumeToServer(int displayLevel) {
  JsonDocument doc;
  doc["action"] = "SET_VOLUME";
  doc["volume"] = constrain(displayLevel, VOLUME_LEVEL_MIN, VOLUME_LEVEL_MAX);

  String payload;
  serializeJson(doc, payload);
  sendServerRequest(payload);
}

static bool isLedColorOff(uint8_t r, uint8_t g, uint8_t b) {
  return r == 0 && g == 0 && b == 0;
}

static void ensureVisibleLedColor() {
  if (!isLedColorOff(ledR, ledG, ledB)) return;

  ledR = 255;
  ledG = 255;
  ledB = 255;
  prefs.putUChar("ledR", ledR);
  prefs.putUChar("ledG", ledG);
  prefs.putUChar("ledB", ledB);
}

static void persistLocalLedState() {
  prefs.putBool("ledEnabled", ledEnabled);
  prefs.putInt("ledBright", ledBrightness);
  prefs.putUChar("ledR", ledR);
  prefs.putUChar("ledG", ledG);
  prefs.putUChar("ledB", ledB);
}

static void syncLocalLedToServer() {
  JsonDocument doc;
  doc["action"] = "SET_LED";
  doc["led_r"] = ledEnabled ? ledR : 0;
  doc["led_g"] = ledEnabled ? ledG : 0;
  doc["led_b"] = ledEnabled ? ledB : 0;
  doc["led_br"] = ledBrightness;
  doc["led_bright"] = ledBrightness;

  String payload;
  serializeJson(doc, payload);
  sendServerRequest(payload);
}

static int blendSinglePage(int scent) {
  switch (scent) {
    case 1: return 10;
    case 2: return 14;
    case 3: return 17;
    case 4: return 19;
    default: return PAGE_BLEND_HOME;
  }
}

static int blendPairPage(const String &selection) {
  if (selection.indexOf("1") >= 0 && selection.indexOf("2") >= 0) return 11;
  if (selection.indexOf("1") >= 0 && selection.indexOf("3") >= 0) return 12;
  if (selection.indexOf("1") >= 0 && selection.indexOf("4") >= 0) return 13;
  if (selection.indexOf("2") >= 0 && selection.indexOf("3") >= 0) return 15;
  if (selection.indexOf("2") >= 0 && selection.indexOf("4") >= 0) return 16;
  if (selection.indexOf("3") >= 0 && selection.indexOf("4") >= 0) return 18;
  return PAGE_BLEND_HOME;
}

static void showWeatherLoadingFields();
static void updateWeatherFields();

static bool isManualScentPage(int pageId) {
  return pageId >= PAGE_MANUAL_SCENT_BASE + 1 && pageId <= PAGE_MANUAL_SCENT_BASE + 4;
}

static bool isBlendPage(int pageId) {
  return pageId >= PAGE_BLEND_HOME && pageId <= 19;
}

static int currentManualPage() {
  if (blendModeEnabled) {
    return isBlendPage(lastManualPage) ? lastManualPage : PAGE_BLEND_HOME;
  }
  return isManualScentPage(lastManualPage) ? lastManualPage : PAGE_MANUAL;
}

static int currentIntensityPage() {
  if (currentIntensity == 1) return PAGE_INTENSITY_LOW;
  if (currentIntensity == 2) return PAGE_INTENSITY_MEDIUM;
  if (currentIntensity == 3) return PAGE_INTENSITY_HIGH;
  return PAGE_INTENSITY_HOME;
}

static int currentLedPage() {
  if (!ledEnabled) return PAGE_LED_OFF;
  if (ledBrightness <= 90) return PAGE_LED_DIM;
  if (ledBrightness >= 220) return PAGE_LED_BRIGHT;
  return PAGE_LED_NORMAL;
}

static void showManualPageByState() {
  showPage(currentManualPage());
}

static void showWeatherPageByState() {
  showPage(PAGE_WEATHER);
  if (currentMode == MODE_WEATHER) {
    updateWeatherFields();
  } else {
    showWeatherLoadingFields();
  }
}

static void clearWeatherState() {
  hasWeatherSnapshot = false;
  hasTempHumiSnapshot = false;
  lastWeatherLabel = "";
  lastWeatherIconId = 0;
}

static void showHomePageByWeatherState() {
  if (currentMode == MODE_WEATHER) {
    showWeatherPageByState();
  } else {
    showPage(PAGE_WEATHER_OFF);
  }
}

void showStartupReadyPage() {
  if (offlineModeActive) {
    showPage(PAGE_OFFLINE);
    return;
  }
  showPage(PAGE_STARTUP_HOME);
}

static void setOfflineModeActive(bool active) {
  offlineModeActive = active;

  if (offlineModeActive) {
    if (currentMode == MODE_WEATHER) {
      clearWeatherState();
      setSystemMode(MODE_READY, "Offline Mode");
    }
    showPage(PAGE_OFFLINE);
  } else {
    showPage(PAGE_WEATHER_OFF);
  }
}

void showLowFluidPage(int cartNum) {
  int returnPage = currentDisplayPage;
  showPage(PAGE_LOW_FLUID);
  
  if (cartNum >= 1 && cartNum <= 4) {
    // Nextion 폰트 한글 출력 지원을 위해 EUC-KR 인코딩 바이트 배열 전송
    // "N번 부족" -> N: '0'+cartNum, 번: B9 F8, 공백: 20, 부: BA CE, 족: C1 B7
    uint8_t encoded[8] = {
      (uint8_t)('0' + cartNum),
      0xB9, 0xF8, 0x20, 0xBA, 0xCE, 0xC1, 0xB7
    };
    nexSerial.print("t_capnum.txt=\"");
    nexSerial.write(encoded, 8);
    nexSerial.print("\"");
    nexWriteTerminator();
  }
  
  delay(3000);
  if (returnPage == PAGE_LOW_FLUID) {
    showHomePageByWeatherState();
  } else {
    showPage(returnPage);
  }
}

static bool sendEncodedRegionText(const String &regionText) {
  for (size_t i = 0; i < sizeof(ENCODED_REGIONS) / sizeof(ENCODED_REGIONS[0]); i++) {
    if (regionText == ENCODED_REGIONS[i].utf8) {
      nexSerial.print("t_region.txt=\"");
      nexSerial.write(ENCODED_REGIONS[i].encoded, ENCODED_REGIONS[i].length);
      nexSerial.print("\"");
      nexWriteTerminator();
      return true;
    }
  }
  return false;
}

static bool sendEncodedWeatherBytes(const uint8_t *encoded, size_t length) {
  nexSerial.print("t_weather.txt=\"");
  nexSerial.write(encoded, length);
  nexSerial.print("\"");
  nexWriteTerminator();
  return true;
}

static bool sendEncodedWeatherText(const String &weatherText) {
  for (size_t i = 0; i < sizeof(ENCODED_WEATHERS) / sizeof(ENCODED_WEATHERS[0]); i++) {
    if (weatherText == ENCODED_WEATHERS[i].utf8) {
      return sendEncodedWeatherBytes(ENCODED_WEATHERS[i].encoded, ENCODED_WEATHERS[i].length);
    }
  }

  if (weatherText.indexOf("\xEB\xB9\x84") >= 0) return sendEncodedWeatherBytes(WEATHER_RAIN, sizeof(WEATHER_RAIN));
  if (weatherText.indexOf("\xEA\xB0\x95\xEC\x88\x98") >= 0) return sendEncodedWeatherBytes(WEATHER_PRECIP, sizeof(WEATHER_PRECIP));
  if (weatherText.indexOf("\xEB\x88\x88") >= 0) return sendEncodedWeatherBytes(WEATHER_SNOW, sizeof(WEATHER_SNOW));
  if (weatherText.indexOf("\xED\x9D\x90\xEB\xA6\xBC") >= 0) return sendEncodedWeatherBytes(WEATHER_CLOUDY, sizeof(WEATHER_CLOUDY));
  if (weatherText.indexOf("\xEA\xB5\xAC\xEB\xA6\x84") >= 0) return sendEncodedWeatherBytes(WEATHER_CLOUD, sizeof(WEATHER_CLOUD));
  if (weatherText.indexOf("\xEB\xA7\x91") >= 0) return sendEncodedWeatherBytes(WEATHER_SUNNY, sizeof(WEATHER_SUNNY));
  if (weatherText.indexOf("\xEC\x95\x8C\x20\xEC\x88\x98\x20\xEC\x97\x86\xEC\x9D\x8C") >= 0) return sendEncodedWeatherBytes(WEATHER_UNKNOWN, sizeof(WEATHER_UNKNOWN));
  if (weatherText.indexOf("\xED\x86\xB5\xEC\x8B\xA0\x20\xEC\x8B\xA4\xED\x8C\xA8") >= 0) return sendEncodedWeatherBytes(WEATHER_FAIL, sizeof(WEATHER_FAIL));
  if (weatherText.indexOf("\xEC\x98\xA4\xEB\xA5\x98") >= 0) return sendEncodedWeatherBytes(WEATHER_ENV_ERR, sizeof(WEATHER_ENV_ERR));

  return false;
}

static int weatherIconFromText(const String &weatherText) {
  String normalized = weatherText;
  normalized.trim();

  if (normalized.indexOf("\xEB\xB9\x84") >= 0 || normalized.indexOf("\xEA\xB0\x95\xEC\x88\x98") >= 0) return 3;
  if (normalized.indexOf("\xEB\x88\x88") >= 0) return 4;
  if (normalized.indexOf("\xED\x9D\x90\xEB\xA6\xBC") >= 0 || normalized.indexOf("\xEA\xB5\xAC\xEB\xA6\x84") >= 0) return 2;
  if (normalized.indexOf("\xEB\xA7\x91") >= 0 || normalized.indexOf("\xED\x95\xB4") >= 0) return 1;
  return 0;
}

static bool hasValidWeatherText(const String &weatherText) {
  String normalized = weatherText;
  normalized.trim();
  return normalized.length() > 0 && normalized != "Loading" && normalized != "Weather";
}

static void setTempFont(int fontId) {
  nexSend("t_temp.font=" + String(fontId));
}

static void sendCachedTempHumi() {
  setTempFont(TEMP_FONT_NORMAL);

  if (!hasTempHumiSnapshot) {
    nexSend("t_temp.txt=\"\"");
    nexSend("t_humi.txt=\"\"");
    return;
  }

  nexSend("t_temp.txt=\"" + String(lastWeatherTempC, 1) + "C\"");
  nexSend("t_humi.txt=\"" + String(lastWeatherHumi, 0) + "%\"");
}

static void showWeatherLoadingFields() {
  nexSend("t_region.txt=\"\"");
  nexSend("t_weather.txt=\"\"");
  setTempFont(TEMP_FONT_LOADING);
  nexSend("t_temp.txt=\"Loading\"");
  nexSend("t_humi.txt=\"\"");
}

static void beginWeatherRefresh() {
  hasWeatherSnapshot = false;
  hasTempHumiSnapshot = false;
  lastWeatherLabel = "";
  lastWeatherIconId = 0;
  showWeatherLoadingFields();
}

static void updateWeatherFields() {
  String regionText = lastWeatherRegion;
  regionText.trim();

  String weatherText = lastWeatherLabel;
  weatherText.trim();

  if (!hasWeatherSnapshot || !hasValidWeatherText(weatherText)) {
    showWeatherLoadingFields();
    return;
  }

  if (regionText.length() == 0) {
    nexSend("t_region.txt=\"\"");
  } else if (!sendEncodedRegionText(regionText)) {
    nexSend("t_region.txt=\"\"");
  }
  if (!sendEncodedWeatherText(weatherText)) {
    sendEncodedWeatherBytes(WEATHER_UNKNOWN, sizeof(WEATHER_UNKNOWN));
  }
  sendCachedTempHumi();
}

// ★ 추가: MAC 주소 기반 고유 디바이스 ID 생성 함수
String getUniqueDeviceId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  return "ESP_" + mac;
}

void initNetworkSession() {
  if (!isSessionInit) { sharedClient.setInsecure(); isSessionInit = true; }
}

void rememberWeatherRegion(const String &region) {
  if (region.length() == 0 || region == lastWeatherRegion) return;

  lastWeatherRegion = region;
  if (currentMode == MODE_WEATHER) {
    hasWeatherSnapshot = false;
    hasTempHumiSnapshot = false;
  }
  prefs.putString("last_region", lastWeatherRegion);
  Serial.printf(C_CYAN "\r\n[Weather] Saved region updated: %s\r\n" C_RESET, lastWeatherRegion.c_str());
}

String buildWeatherRequestPayload(const String &region) {
  JsonDocument reqDoc;
  reqDoc["mode"] = "weather";
  String safeRegion = region;
  safeRegion.trim();
  if (safeRegion.length() > 0) {
    reqDoc["region"] = safeRegion;
  }

  // 🔥 날씨 모드에서도 현재 무게 배열을 추가해서 서버로 같이 보냄!
  JsonArray wArray = reqDoc["weights"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    wArray.add((float)((int)(weights[i] * 10)) / 10.0);
  }

  String payload;
  payload.reserve(128); // JSON 크기가 늘어났으니 메모리 블록 128로 세팅
  serializeJson(reqDoc, payload);
  return payload;
}

static String buildDisplayModePayload(const String &activeMode, int activeScent = 0) {
  JsonDocument reqDoc;
  reqDoc["action"] = "DISPLAY_MODE";
  reqDoc["mode"] = "display";
  reqDoc["active_mode"] = activeMode;
  if (activeScent > 0) {
    reqDoc["active_scent"] = activeScent;
  }

  String payload;
  payload.reserve(96);
  serializeJson(reqDoc, payload);
  return payload;
}

static void syncDisplayModeToServer(const String &activeMode, int activeScent = 0) {
  if (WiFi.status() != WL_CONNECTED) return;
  sendServerRequest(buildDisplayModePayload(activeMode, activeScent));
}

static String attachDeviceIdIfMissing(const String &payload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err || !doc.is<JsonObject>()) return payload;

  if (doc["deviceId"].isNull()) {
    doc["deviceId"] = deviceId; // ★ 수정: 사용자가 설정한 기기 ID 변수를 전송
  }

  String normalized;
  normalized.reserve(payload.length() + 32);
  serializeJson(doc, normalized);
  return normalized;
}

static bool readJsonFloatField(JsonDocument &doc, const char *fieldName, float &value) {
  if (doc[fieldName].isNull()) return false;

  String rawValue = doc[fieldName].as<String>();
  rawValue.trim();
  if (rawValue.length() == 0) return false;

  value = rawValue.toFloat();
  return true;
}

static String findNextionCmd(String buf) {
  buf.trim();
  if (buf.length() != 2) return "";

  const char* tokens[] = {
    "MM", "MO", "MD", "DS", "IM", "WR",
    "LN", "LO", "L0", "LF", "L1", "L2", "L3",
    "M1", "M2", "M3",
    "Y1", "Y2", "Y3",
    "I1", "I2", "I3", "IH",
    "VU", "VD", "VM",
    "ST", "BM", "RE", "OF", "TZ", "WK",
    "S1", "S2", "S3", "S4"
  };

  for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++) {
    if (buf.indexOf(tokens[i]) >= 0) return String(tokens[i]);
  }
  return "";
}

static String processNextionChar(char c, String &buf, bool executeCmd) {
  if (isalnum((unsigned char)c) || c == '_' || c == ' ') {
    buf += c;
  } else {
    buf = "";
    return "";
  }

  String cmd = findNextionCmd(buf);
  if (cmd.length() > 0) {
    if (executeCmd) handleNextionCmd(cmd);
    buf = "";
    return cmd;
  }

  if (buf.length() > 64) buf = "";
  return "";
}

void checkNextionInput() {
  static String buf = ""; 
  unsigned long startMillis = millis();

  while (nexSerial.available()) {
    char c = (char)nexSerial.read();
    if ((c >= 'A' && c <= 'Z') || c == 0x65 || c == 0x67 || c == 0x68) {
      lastActivityTime = millis(); // 터치/버튼 입력이 들어온 순간 절전 타이머 리셋
    }

    String cmd = processNextionChar(c, buf, false);
    if (cmd.length() == 0) {
      if (millis() - startMillis > 50) break;
      continue;
    }

    if (cmd == "WK") {
      wakeUpSystem();
      if (millis() - startMillis > 50) break;
      continue;
    }

    handleNextionCmd(cmd);
    wakeUpSystem();
    
    if (millis() - startMillis > 50) break;
  }

  if (pendingManualScent.length() > 0 && millis() - lastScentTouchTime > 300) {
    runManualMode(pendingManualScent);
    pendingManualScent = "";
  }
}

// =================================================================
// 🖥️ [분리된 헬퍼 함수 1] 블렌딩 모드(BM) 켜기/끄기 토글 처리
// =================================================================
static void toggleBlendMode() {
  blendModeEnabled = !blendModeEnabled;
  blendSelection = "";
  blendSprayActive = false;

  if (blendModeEnabled) {
    nexSend("b_blend.txt=\"blendOFF\"");
    lastManualPage = PAGE_BLEND_HOME;
    showPage(PAGE_BLEND_HOME);
    Serial.println("\r\n[BLEND] ON");
  } else {
    nexSend("b_blend.txt=\"blendON\"");
    lastManualPage = PAGE_MANUAL;
    showPage(PAGE_MANUAL);
    Serial.println("\r\n[BLEND] OFF");
  }
}

// =================================================================
// 🖥️ [분리된 헬퍼 함수 2] S1~S4 향수 분사 버튼 처리
// =================================================================
static void handleScentButton(const String &cmd) {
  int scent = cmd.substring(1).toInt();
  if (scent < 1 || scent > 4) return;

  Serial.printf("\r\n[S BUTTON] %s\r\n", cmd.c_str());
  currentMode = MODE_MANUAL;

  // 일반 모드일 경우 즉시 분사
  if (!blendModeEnabled) {
    lastManualPage = PAGE_MANUAL_SCENT_BASE + scent;
    showPage(lastManualPage);
    syncDisplayModeToServer("manual", scent);
    pendingManualScent = String(scent);
    lastScentTouchTime = millis();
    return;
  }

  // 블렌딩 모드일 경우 번호 조합
  String scentStr = String(scent);
  if (blendSelection.indexOf(scentStr) == -1) {
    blendSelection += scentStr;
  }

  if (blendSelection.length() == 1) {
    lastManualPage = blendSinglePage(scent);
    showPage(lastManualPage);
  } else if (blendSelection.length() == 2) {
    lastManualPage = blendPairPage(blendSelection);
    showPage(lastManualPage);
    syncDisplayModeToServer("manual", blendSelection.toInt());
    
    pendingManualScent = blendSelection;
    lastScentTouchTime = millis();
    blendSelection = "";
  }
}

// =================================================================
// 🖥️ [메인 라우터] 넥션 디스플레이 명령어 분배기
// =================================================================
static void handleIntensityButton(const String &cmd) {
  int intensity = (cmd == "IH") ? 3 : cmd.substring(1).toInt();
  if (intensity < 1 || intensity > 3) return;

  SprayIntensity(intensity);
  markLocalSettingsChanged();
  syncLocalIntensityToServer();
  showPage(currentIntensityPage());

  const char *label = (currentIntensity == 1) ? "Low" : (currentIntensity == 2) ? "Medium" : "High";
  updateDisplay(0, String("Intensity ") + label);
  Serial.printf("\r\n[Display] Intensity: %d, spray 3s / cooldown %lds\r\n", currentIntensity, REST_TIME / 1000);
}

static int getDisplayVolumeLevel() {
  return constrain((currentVolume + 1) / 3, 0, 10);
}

static void showDisplayVolumePage(int level) {
  int displayLevel = constrain(level, VOLUME_LEVEL_MIN, VOLUME_LEVEL_MAX);
  showPage(VOLUME_PAGE_BASE + displayLevel);
}

static void setDisplayVolumeLevel(int level) {
  int displayLevel = constrain(level, 0, 10);
  changeVolume(displayLevel * 3);
  markLocalSettingsChanged();
  syncLocalVolumeToServer(displayLevel);
  showDisplayVolumePage(displayLevel);
  Serial.printf("\r\n[Display] Volume level: %d/10 (DFPlayer %d/30)\r\n", displayLevel, currentVolume);
}

static void handleVolumeButton(const String &cmd) {
  if (cmd == "VU") {
    setDisplayVolumeLevel(getDisplayVolumeLevel() + 1);
  } else if (cmd == "VD") {
    setDisplayVolumeLevel(getDisplayVolumeLevel() - 1);
  } else if (cmd == "VM") {
    showDisplayVolumePage(getDisplayVolumeLevel());
  }
}

static void showCurrentLedPage() {
  showPage(currentLedPage());
}

static void setDisplayLedEnabled(bool enabled) {
  ledEnabled = enabled;
  if (ledEnabled && ledBrightness <= 0) {
    ledBrightness = 150;
  }
  if (ledEnabled) {
    ensureVisibleLedColor();
  }
  persistLocalLedState();
  markLocalSettingsChanged();
  syncLocalLedToServer();
  showPage(ledEnabled ? currentLedPage() : PAGE_LED_OFF);
  setLedColor(ledEnabled ? ledR : 0, ledEnabled ? ledG : 0, ledEnabled ? ledB : 0);
  Serial.printf("\r\n[Display] LED %s (R:%d G:%d B:%d brightness=%d)\r\n",
                ledEnabled ? "ON" : "OFF", ledR, ledG, ledB, ledBrightness);
}

static void setDisplayLedBrightness(int brightness, int pageId) {
  ledEnabled = true;
  ledBrightness = constrain(brightness, 0, 255);
  ensureVisibleLedColor();
  persistLocalLedState();
  markLocalSettingsChanged();
  syncLocalLedToServer();
  showPage(pageId);
  setLedColor(ledR, ledG, ledB);
}

static void handleLedButton(const String &cmd) {
  if (cmd == "LO" || cmd == "L0") {
    setDisplayLedEnabled(true);
  } else if (cmd == "LF") {
    setDisplayLedEnabled(false);
  } else if (cmd == "L1") {
    setDisplayLedBrightness(60, PAGE_LED_DIM);
  } else if (cmd == "L2") {
    setDisplayLedBrightness(150, PAGE_LED_NORMAL);
  } else if (cmd == "L3") {
    setDisplayLedBrightness(255, PAGE_LED_BRIGHT);
  }
}

void handleNextionCmd(const String &cmd) {
  // 1. 화면 터치 노이즈 및 UI 업데이트 응답(쓰레기값) 무시
  if (cmd.startsWith("p0") || cmd.startsWith("t_weather") || cmd.startsWith("j0")) return;

  // ★ 2. 글로벌 UI 쿨타임 방어 (버튼 연속 터치 버그 방지)
  static unsigned long lastValidCmdMillis = 0;
  bool isVolumeCmd = cmd.startsWith("V"); // 볼륨 슬라이더는 연속 입력 허용
  unsigned long cooldown = isVolumeCmd ? 50 : 1000; // 일반 버튼은 1초(1000ms) 쿨타임 적용

  // 쿨타임을 무시하고 즉시 반응해야 하는 화면 전환/모드 변경 커맨드들
  bool isNoCooldownCmd = (cmd == "ST" || cmd == "WR" || cmd == "BM" || cmd == "DS" || cmd == "OF" || cmd.startsWith("M") || cmd.startsWith("Y"));

  if (!isNoCooldownCmd && millis() - lastValidCmdMillis < cooldown) {
    Serial.printf("\r\n[UI] 쿨타임 방어: %s 명령 무시됨\r\n", cmd.c_str());
    return;
  }
  lastValidCmdMillis = millis();

  Serial.printf("\r\n[Nextion] CMD: %s\r\n", cmd.c_str());

  if (cmd == "MM" || cmd == "MO" || cmd == "MD") {
    showPage(PAGE_MODE_SELECT);
  }
  else if (cmd == "OF") {
    setOfflineModeActive(!offlineModeActive);
  }
  else if (cmd == "DS") {
    showPage(PAGE_DEVICE_STATUS);
    updateScentProgressBars();
  }
  else if (cmd == "IM") {
    showPage(currentIntensityPage());
  }
  else if (cmd == "WR") {
    prefs.putInt("wifi_return_page", currentDisplayPage);
    showPage(PAGE_WIFI_RESET);
  }
  else if (cmd == "M2") {
    if (offlineModeActive) {
      showPage(PAGE_WEATHER_OFF);
    } else if (currentMode == MODE_WEATHER) {
      showWeatherPageByState();
    } else {
      showPage(PAGE_WEATHER_OFF);
    }
  }
  else if (cmd == "Y3") {
    if (offlineModeActive) {
      showPage(PAGE_OFFLINE);
    } else if (currentMode == MODE_WEATHER) {
      showWeatherPageByState();
    } else {
      showPage(PAGE_WEATHER);
      beginWeatherRefresh();
      enterWeatherMode(true);
      syncDisplayModeToServer("weather");
    }
  } 
  else if (cmd == "M1" || cmd == "Y1") {
    showManualPageByState();
    if (currentMode != MODE_WEATHER) {
      setSystemMode(MODE_MANUAL, "Manual Mode");
    }
  } 
  else if (cmd == "M3" || cmd == "Y2") {
    showPage(PAGE_DEVICE_STATUS);
    updateScentProgressBars();
    if (currentMode != MODE_WEATHER) {
      setSystemMode(MODE_SETTING, "Setting Mode");
    }
  } 
  else if (cmd == "ST") {
    SystemMode modeBeforeStop = currentMode;
    lastManualPage = PAGE_MANUAL;
    setSystemMode(MODE_READY, "Stopped");
    pendingManualScent = "";

    if (modeBeforeStop == MODE_WEATHER) {
      hasWeatherSnapshot = false;
      hasTempHumiSnapshot = false;
      lastWeatherLabel = "";
      lastWeatherIconId = 0;
      syncDisplayModeToServer("ready");
      showPage(PAGE_WEATHER_OFF);
    } else if (modeBeforeStop == MODE_MANUAL) {
      lastStoppedManualScent = isManualScentPage(currentDisplayPage) ? (currentDisplayPage - PAGE_MANUAL_SCENT_BASE) : 0;
      manualModeOffMillis = millis();
      syncDisplayModeToServer("ready");
      showPage(PAGE_MANUAL);
    } else {
      showPage(PAGE_MODE_SELECT);
    }
  } 
  else if (cmd == "BM") {
    toggleBlendMode(); // 👉 분리된 헬퍼 함수 호출
  } 
  else if (cmd.startsWith("I")) {
    handleIntensityButton(cmd);
  }
  else if (cmd.startsWith("V")) {
    handleVolumeButton(cmd);
  }
  else if (cmd.startsWith("L")) {
    if (cmd == "LN") showCurrentLedPage();
    else handleLedButton(cmd);
  }
  else if (cmd.startsWith("S")) {
    handleScentButton(cmd); // 👉 분리된 헬퍼 함수 호출
  } 
  else if (cmd == "RE") {
    Serial.println("Wi-Fi 설정 초기화 및 재부팅!");
    int returnPage = prefs.getInt("wifi_return_page", currentDisplayPage);
    prefs.putInt("wifi_return_page", returnPage);
    prefs.putBool("force_config_portal", true);
    prefs.putBool("force_offline", false);
    showPage(PAGE_WIFI_RESET);
    WiFiManager wifiManager;
    wifiManager.resetSettings(); // 플래시 메모리 와이파이 정보 삭제
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(700);
    ESP.restart();
  } 
  else if (cmd == "TZ") {
    int returnPage = currentDisplayPage;
    showPage(PAGE_TARE);
    resetScaleZero();
    delay(1000);
    showPage(returnPage == PAGE_TARE ? PAGE_DEVICE_STATUS : returnPage);
    if (returnPage == PAGE_DEVICE_STATUS) {
      updateScentProgressBars();
    }
  }
}

void nexSend(const String &cmd) { nexSerial.print(cmd); nexSerial.write(0xFF); nexSerial.write(0xFF); nexSerial.write(0xFF); }
void updateDisplay(int iconID, String text) { 
  if (currentMode == MODE_WEATHER && lastWeatherIconId >= 1 && lastWeatherIconId <= 4) iconID = lastWeatherIconId;
  nexSend("p0.pic=" + String(iconID));

  if (currentMode == MODE_WEATHER) {
    updateWeatherFields();
    return;
  }

  if (text.length() == 0) return;
}

// =================================================================
// 🕒 디스플레이 상단 시계(시간) 업데이트 함수
// =================================================================
static bool volumeNeedsSave = false;
static unsigned long lastVolumeChangeTime = 0;

void updateClockDisplay() {
  if (pendingPageUpdate && millis() - pageTransitionTime >= 80) {
    pendingPageUpdate = false;
    sendClockDisplayNow();
    
    if (transitioningPageId == PAGE_MANUAL || transitioningPageId == PAGE_DEVICE_STATUS || 
        isManualScentPage(transitioningPageId) || isBlendPage(transitioningPageId)) {
      updateScentProgressBars();
    }
  }

  if (volumeNeedsSave && millis() - lastVolumeChangeTime > 3000) {
    prefs.putInt("volume", currentVolume);
    volumeNeedsSave = false;
    Serial.println(C_CYAN "[System] 볼륨 설정이 자동 저장되었습니다." C_RESET);
  }

  static unsigned long lastClockUpdate = 0;

  // 1초마다 한 번씩만 업데이트 (과부하 방지)
  if (millis() - lastClockUpdate < 1000) return;
  lastClockUpdate = millis();

  sendClockDisplayNow();
}

void updateTempHumi(float tempC, float humi) {
  lastWeatherTempC = tempC;
  lastWeatherHumi = humi;
  hasTempHumiSnapshot = true;
  setTempFont(TEMP_FONT_NORMAL);
  nexSend("t_temp.txt=\"" + String(tempC, 1) + "C\"");
  nexSend("t_humi.txt=\"" + String(humi, 0) + "%\"");
}
void updateProgressBar(int val) { updateProgressBar(1, val); }

void updateProgressBar(int barIndex, int val) {
  int safeIndex = constrain(barIndex, 1, 4);
  nexSend("j" + String(safeIndex) + ".val=" + String(constrain(val, 0, 100)));
}

float calculateScentPercent(float weightValue) {
  // Globals.h에 정의된 정확한 이름을 사용합니다. 
  const float emptyWeight = Config::EMPTY_WEIGHT; 
  const float fallbackFullWeight = 95.4f; // [cite: 1]
  
  // 현재 설정된 최대 무게(maxWeight)가 빈 병보다 무거울 때만 사용합니다. [cite: 1]
  const float fullWeight = (maxWeight > emptyWeight) ? maxWeight : fallbackFullWeight;
  const float usableWeight = fullWeight - emptyWeight;

  // 분모가 0이 되는 것을 방지합니다. [cite: 1]
  if (usableWeight <= 0.0f) return 0.0f;

  // 측정값이 설정된 빈 병 무게보다 작으면 0으로 처리합니다. [cite: 1]
  float clampedWeight = (weightValue < emptyWeight) ? emptyWeight : weightValue;
  
  // 퍼센트 계산 후 0~100 범위로 제한합니다. [cite: 1]
  float percent = ((clampedWeight - emptyWeight) / usableWeight) * 100.0f;
  return constrain(percent, 0.0f, 100.0f);
}

void updateScentProgressBars() {
  for (int i = 0; i < 4; i++) {
    int percent = constrain((int)round(calculateScentPercent(weights[i])), 0, 100);
    updateProgressBar(i + 1, percent);
    nexSend("t_percent" + String(i + 1) + ".txt=\"" + String(percent) + "%\"");
  }
}

// 👇 1. connectWiFi 함수 바로 위에 '디스플레이 감시자 태스크'를 추가합니다.
void displayWatcherTask(void *pvParameters) {
  for(;;) {
    if (nexSerial.available()) {
      char c = (char)nexSerial.read();
      static String tempBuf = "";
      String cmd = processNextionChar(c, tempBuf, false);
      
      // 디스플레이에서 OF 신호가 들어온 것을 실시간으로 포착!
      if (cmd.length() > 0) {
        if (cmd == "OF") {
          Serial.println(C_GREEN "\r\n🔌 [Offline] 오프라인 버튼 터치 감지! 수동 모드로 즉시 전환합니다..." C_RESET);
          prefs.putBool("force_offline", true);
          prefs.remove("pending_nextion_cmd");
        } else if (cmd == "RE") {
          Serial.println(C_GREEN "\r\n[WiFi] RE 버튼 터치 감지! Wi-Fi 설정 포털을 강제 시작합니다..." C_RESET);
          prefs.putInt("wifi_return_page", prefs.getInt("wifi_return_page", currentDisplayPage));
          prefs.putBool("force_config_portal", true);
          prefs.putBool("force_offline", false);
          prefs.remove("pending_nextion_cmd");
          showPage(PAGE_WIFI_RESET);
          WiFiManager wifiManager;
          wifiManager.resetSettings();
          WiFi.disconnect(true, true);
        } else {
          prefs.putBool("force_offline", true);
          prefs.putString("pending_nextion_cmd", cmd);
        }
        delay(700);
        
        // 블로킹을 강제로 깨기 위해 시스템 즉시 재부팅 (0.5초 컷)
        ESP.restart(); 
      }
      if (tempBuf.length() > 20) tempBuf = ""; 
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
    // 코어 독점 방지용 딜레이
    vTaskDelay(50 / portTICK_PERIOD_MS); 
  }
}

// 👇 2. 기존 connectWiFi 함수를 업그레이드 합니다.
void connectWiFi() {
  Serial.printf("\r\n[System] 갤럭시 팝업 강제 트리거 모드 가동...\r\n");
  
  esp_task_wdt_delete(NULL);

  // ★★★ [핵심 1] 오프라인 티켓 확인 ★★★
  if (prefs.getBool("force_offline", false)) {
    // 티켓을 사용했으니 다음 부팅을 위해 찢어버립니다 (초기화)
    prefs.putBool("force_offline", false); 
    
    Serial.println(C_YELLOW "\r\n[Offline] 사용자의 요청으로 Wi-Fi 설정을 건너뛰고 수동 모드로 진입합니다!" C_RESET);
    setOfflineModeActive(true);
    
    esp_task_wdt_add(NULL);
    return; // 와이파이매니저 자체를 실행하지 않고 여기서 바로 함수를 빠져나갑니다!
  }

  WiFiManager wifiManager;
  bool forceConfigPortal = prefs.getBool("force_config_portal", false);
  if (forceConfigPortal) {
    prefs.putBool("force_config_portal", false);
    showPage(PAGE_WIFI_RESET);
    WiFi.disconnect(true, true);
    delay(200);
  }

  // ★★★ [핵심 2] 사용자 요청대로 무한정 대기 (0으로 설정) ★★★
  wifiManager.setConfigPortalTimeout(forceConfigPortal ? 0 : 120);

  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  wifiManager.setCaptivePortalEnable(true); 
  WiFi.setHostname("SmartDiffuser");
  wifiManager.setDebugOutput(true);

  WiFi.mode(WIFI_STA); 
  String savedSSID = WiFi.SSID();

  if (savedSSID.length() > 0) {
    Serial.printf("\r\n[WiFi] 저장된 와이파이 발견: %s\r\n", savedSSID.c_str());
    Serial.println("[WiFi] 연결을 무한 대기합니다. 건너뛰려면 디스플레이의 OF 버튼을 누르세요.");
  } else {
    Serial.println("\r\n[WiFi] 최초 부팅 상태입니다! (와이파이 미설정)");
    Serial.println("[WiFi] 설정될 때까지 무한 대기합니다. 수동 모드로 가려면 OF 버튼을 누르세요.");
  }

  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("\r\n[WiFiManager] 설정(AP) 모드 시작. 'SmartDiffuser_Setup' 연결 가능");
    updateDisplay(0, "Setup WiFi");
  });

  // ★★★ [핵심 3] 와이파이매니저가 실행(블로킹)되기 직전에 감시자 태스크를 백그라운드에 투입! ★★★
  TaskHandle_t watcherHandle = NULL;
  clearNextionInputBuffer();
  xTaskCreatePinnedToCore(displayWatcherTask, "WatcherTask", 2048, NULL, 1, &watcherHandle, 0);

  // 여기서부터 코드가 멈추고 와이파이를 무한정 기다리지만, 백그라운드의 감시자는 계속 일합니다.
  bool wifiConnected = false;
  if (forceConfigPortal) {
    Serial.println("\r\n[WiFiManager] RE 버튼 요청으로 설정 포털을 강제 시작합니다.");
    updateDisplay(0, "Setup WiFi");
    wifiConnected = wifiManager.startConfigPortal("SmartDiffuser_Setup");
  } else {
    wifiConnected = wifiManager.autoConnect("SmartDiffuser_Setup");
  }

  if (!wifiConnected) {
    Serial.println(C_YELLOW "\r\n[Warning] Wi-Fi 설정 실패." C_RESET);
  } else {
    Serial.printf("\r\nConnected! IP: %s\r\n", WiFi.localIP().toString().c_str());
    if (prefs.isKey("wifi_return_page")) {
      int returnPage = prefs.getInt("wifi_return_page", PAGE_WEATHER_OFF);
      prefs.remove("wifi_return_page");
      if (returnPage == PAGE_WIFI_RESET) returnPage = PAGE_WEATHER_OFF;
      showPage(returnPage);
    }
  }

  // 와이파이 연결이 무사히 끝났다면 감시자 태스크는 임무를 다했으므로 삭제해줍니다.
  if (watcherHandle != NULL) {
    vTaskDelete(watcherHandle);
  }
  
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
  if (currentMode == MODE_DEMO || isCommunicate) return; // 통신 중복 방지 
  
  unsigned long now = millis();

  // [최적화] 3초마다 소음 샘플링 (15초 주기의 1/5)
  static unsigned long lastSampleTime = 0;
  static int dbSamples[5] = {0, 0, 0, 0, 0};
  static int sampleIdx = 0;

  if (now - lastSampleTime >= 3000) {
    lastSampleTime = now;
    // ⚠️ 파트너님이 실수로 여길 바꾸셨었어요! 다시 currentDbLevel 로 복구합니다.
    dbSamples[sampleIdx] = currentDbLevel; 
    sampleIdx = (sampleIdx + 1) % 5;
  }

  if (now - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = now;
    
    // 5회 측정 평균 계산
    int dbSum = 0;
    for(int i = 0; i < 5; i++) dbSum += dbSamples[i];
    int avgDb = dbSum / 5;
    
    if (WiFi.status() == WL_CONNECTED) {
      JsonDocument doc; 
      doc["action"] = "POLL";
      doc["deviceId"] = deviceId;
      
      // ✅ 우리가 진짜로 바꿔야 했던 곳은 여기입니다! (lastWebMessage 로 변경 완료)
      doc["status"] = lastWebMessage; 
      
      doc["db_level"] = avgDb; // 15초 평균값 보고
      doc["volume"] = currentVolume / 3; // 앱 규격(0~10)으로 변환하여 보고
      doc["led_br"] = ledEnabled ? ledBrightness : 0;
      
      JsonArray wArray = doc["weights"].to<JsonArray>(); 
      for (int i = 0; i < 4; i++) {
        // 무게 퍼센트 계산 후 소수점 한자리로 정리 
        float percent = calculateScentPercent(weights[i]); 
        wArray.add(round(percent * 10.0f) / 10.0f); 
      }
      
      String payload;
      serializeJson(doc, payload);
      sendServerRequest(payload); // 서버로 전송 큐 삽입 
    }
  }
}

// =================================================================
// 📡 [수정됨] 서버 응답(JSON)을 해석하고 LED 설정까지 포함하여 실행
// =================================================================
static void processServerResponse(const String& response) {
  JsonDocument doc;
  if (deserializeJson(doc, response)) return; 

  // 1. 발향 강도 및 스피커 볼륨 설정 (명령 실행 전 우선 처리)
  if (canApplyServerSettings() && !doc["intensity"].isNull()) {
    SprayIntensity(doc["intensity"] | 2);
  }

  if (canApplyServerSettings() && !doc["volume"].isNull()) {
    int vol = doc["volume"] | 5;
    int scaledVol = (vol <= VOLUME_LEVEL_MAX) ? (vol * 3) : vol;
    scaledVol = constrain(scaledVol, 0, 30);
    if (scaledVol != currentVolume) {
      changeVolume(scaledVol);
      showPage(VOLUME_PAGE_BASE + (scaledVol / 3));
    }
  }

  // 2. 날씨 및 지역 정보 업데이트
  String weatherText = doc["weather"] | "";
  String targetRegion = doc["target_region"] | "";
  if (targetRegion.length() == 0 && lastWeatherRegion.length() == 0 && weatherText.length() > 0) {
    targetRegion = doc["region"] | "";
  }
  bool hadSavedRegion = lastWeatherRegion.length() > 0;
  bool deferWeatherActionForRegionSync = false;

  if (targetRegion.length() > 0) {
    rememberWeatherRegion(targetRegion);
    if (currentMode == MODE_WEATHER && !hadSavedRegion) {
      lastWeatherCallMillis = millis();
      deferWeatherActionForRegionSync = true;
      sendServerRequest(buildWeatherRequestPayload(lastWeatherRegion));
    }
  }

  if (weatherText.length() > 0) {
    lastWeatherLabel = weatherText;
    lastWeatherIconId = weatherIconFromText(weatherText);
    hasWeatherSnapshot = hasValidWeatherText(lastWeatherLabel);
    if (currentMode == MODE_WEATHER) {
      updateDisplay(lastWeatherIconId, "");
    }
  }

  // 3. 온습도 업데이트
  float tempC = lastWeatherTempC;
  float humi = lastWeatherHumi;
  bool hasTemp = readJsonFloatField(doc, "temp", tempC) || readJsonFloatField(doc, "temperature", tempC);
  bool hasHumi = readJsonFloatField(doc, "humidity", humi) ||
                 readJsonFloatField(doc, "humid", humi) ||
                 readJsonFloatField(doc, "humi", humi);
  if (hasTemp || hasHumi) {
    updateTempHumi(tempC, humi);
  }

  // 4. 분사 제어 명령 처리
  String serverActiveMode = doc["active_mode"] | "";
  serverActiveMode.trim();
  static int lastSyncedManualScent = 0;
  if (serverActiveMode == "weather") {
    bool wasWeatherMode = currentMode == MODE_WEATHER;
    if (!wasWeatherMode) {
      setSystemMode(MODE_WEATHER, "Weather Mode");
      showWeatherPageByState();
      lastWeatherCallMillis = millis();
      sendServerRequest(buildWeatherRequestPayload(lastWeatherRegion));
    }
  } else if (serverActiveMode == "ready" || serverActiveMode == "off") {
    if (currentMode == MODE_WEATHER) {
      clearWeatherState();
      setSystemMode(MODE_READY, "Weather Mode Off");
      showPage(PAGE_WEATHER_OFF);
    } else if (currentMode == MODE_MANUAL) {
      setSystemMode(MODE_READY, "Manual Mode Off");
      lastManualPage = PAGE_MANUAL;
    }
    lastSyncedManualScent = 0;
    manualModeOffMillis = 0;
    lastStoppedManualScent = 0;
  } else if (serverActiveMode == "manual") {
    int activeScent = doc["active_scent"] | 0;
    if (activeScent <= 0) {
      activeScent = doc["spray"] | 0;
    }
    int firstScent = activeScent;
    while (firstScent > 9) {
      firstScent /= 10;
    }

    if (firstScent >= 1 && firstScent <= 4) {
      if (lastStoppedManualScent == activeScent && millis() - manualModeOffMillis < 5000) {
        return;
      }
      bool scentChanged = activeScent != lastSyncedManualScent;
      lastSyncedManualScent = activeScent;
      lastManualPage = PAGE_MANUAL_SCENT_BASE + firstScent;
      if (currentMode != MODE_MANUAL || scentChanged) {
        setSystemMode(MODE_MANUAL, "Manual Mode");
        showPage(lastManualPage);
      }
    } else if (currentMode != MODE_MANUAL) {
      setSystemMode(MODE_MANUAL, "Manual Mode");
      showManualPageByState();
    }
  }

  int cmd = doc["spray"] | -1;
  String resultText = doc["result_text"] | "";
  
  if (!doc["music_tracks"].isNull()) {
    updateMusicMapping(doc["music_tracks"] | "");
  }

  if (deferWeatherActionForRegionSync) {
    Serial.println(C_YELLOW "\r\n[Weather] Region synced; skipping this bootstrap spray/music response.\r\n" C_RESET);
  } else if (cmd == 90) {
    clearWeatherState();
    setSystemMode(MODE_READY, "Stopped by Server");
    showPage(PAGE_WEATHER_OFF);
  } else if (cmd > 0) {
    int dur = doc["duration"] | 3;
    int music = doc["music"] | 0;
    String txt = resultText.length() > 0 ? resultText : "명령 수신";
    triggerSpray(cmd, dur, music, txt, (currentMode == MODE_WEATHER));
  }
  // ★ [핵심 수정] 서버에서 명확하게 STOP 또는 OFF 명령이 내려왔을 때만 정지하도록 보강!
  else if (cmd == 0 && (resultText == "STOP" || resultText == "OFF")) {
    SystemMode modeBeforeStop = currentMode;
    setSystemMode(MODE_READY, "Stopped by Server");
    if (modeBeforeStop == MODE_WEATHER) {
      hasWeatherSnapshot = false;
      hasTempHumiSnapshot = false;
      lastWeatherLabel = "";
      lastWeatherIconId = 0;
      showPage(PAGE_WEATHER_OFF);
    } else {
      updateDisplay(0, "Stopped by Server");
    }
    Serial.println(C_YELLOW "\r\n🛑 서버 명령으로 시스템 강제 정지\r\n" C_RESET);
  }
  else if (!doc["music"].isNull()) {
    int track = doc["music"] | 0;
    if (track > 0) {
      playSound(track);
      nexSend("t_music.txt=\"" + getTrackName(track) + "\"");
      updateDisplay(0, "Music Playing");
      Serial.printf(C_GREEN "\r\n🎵 서버 단독 명령으로 %d번 오디오 트랙을 재생합니다.\r\n" C_RESET, track);
    }
  }

  // 5. 타이머(스케줄러) 설정
  if (!doc["timer_enabled"].isNull()) {
    schedulerEnabled = doc["timer_enabled"] | false;
    activeStartHour = doc["timer_start"] | 9;
    activeEndHour = doc["timer_end"] | 22;
  }

  // 6. 서버 명령으로 LED 색상 및 밝기 실시간 변경
  if (canApplyServerSettings() && !doc["led_r"].isNull() && !doc["led_g"].isNull() && !doc["led_b"].isNull()) {
    uint8_t newR = constrain(doc["led_r"] | 255, 0, 255);
    uint8_t newG = constrain(doc["led_g"] | 255, 0, 255);
    uint8_t newB = constrain(doc["led_b"] | 255, 0, 255);
    int newBright = ledBrightness;
    
    if (!doc["led_bright"].isNull()) {
      newBright = constrain(doc["led_bright"] | 150, 0, 255);
    } else if (!doc["led_br"].isNull()) {
      newBright = constrain(doc["led_br"] | 150, 0, 255);
    }

    ledR = newR;
    ledG = newG;
    ledB = newB;
    ledBrightness = newBright;
    ledEnabled = newBright > 0 && !isLedColorOff(ledR, ledG, ledB);

    prefs.putInt("ledBright", ledBrightness);
    prefs.putUChar("ledR", ledR);
    prefs.putUChar("ledG", ledG);
    prefs.putUChar("ledB", ledB);
    prefs.putBool("ledEnabled", ledEnabled);
    setLedColor(ledEnabled ? ledR : 0, ledEnabled ? ledG : 0, ledEnabled ? ledB : 0);

    Serial.printf(C_GREEN "\r\n🎨 서버 명령으로 LED 변경: R:%d G:%d B:%d 밝기:%d ON:%d\r\n" C_RESET,
                  ledR, ledG, ledB, ledBrightness, ledEnabled);
    
    if (currentMode != MODE_SLEEP && currentMode != MODE_VOICE && !isWaitingForTestDb) {
      showPrompt();
    }
  }
}

// =================================================================
// 📡 [메인 태스크] 큐(Queue)에서 메시지를 꺼내 서버로 전송만 담당 (다이어트 성공!)
// =================================================================
void networkTaskLoop(void *pvParameters) {
  for (;;) {
    String* currentMsgPtr;
    
    // 큐에 보낼 메시지가 들어올 때까지 대기 (CPU 점유율 0%)
    if (xQueueReceive(networkQueue, &currentMsgPtr, portMAX_DELAY) == pdTRUE) {
      String payload = *currentMsgPtr;
      delete currentMsgPtr; // 메모리 누수 방지

      isCommunicate = true; 
      initNetworkSession(); 
      HTTPClient http; 
      http.setTimeout(10000); 
      
      if (http.begin(sharedClient, serverName)) {
        http.addHeader("Content-Type", "application/json");
        int httpCode = http.POST(payload);
        
        if (httpCode == HTTP_CODE_OK) {
          String response = http.getString();
          processServerResponse(response); // 👉 분리된 함수로 응답 데이터 토스!
        } else {
          Serial.printf(C_RED "⚠️ [Network] HTTP 에러 코드: %d\r\n" C_RESET, httpCode);
        }
        http.end();
      } else {
        Serial.println(C_RED "⚠️ [Network] 서버 연결 실패" C_RESET);
      }
      isCommunicate = false;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// 입구컷 안 당하게 큐에 넣는 방식으로 변경
void sendServerRequest(String payload) {
  if (WiFi.status() != WL_CONNECTED) return;

  String normalizedPayload = attachDeviceIdIfMissing(payload);
  String* msg = new String(normalizedPayload);
  if (xQueueSend(networkQueue, &msg, 0) != pdTRUE) {
    delete msg; // 큐가 꽉 찼으면 메모리 해제
    Serial.println(C_RED "⚠️ 통신 대기열이 꽉 찼습니다!" C_RESET);
  }
}

// --- 최적화 2: 오디오 버퍼 메모리 할당 방식 개선 (ESP-IDF API 적용) ---
bool recordAudio(uint8_t** audioBuffer, uint32_t* totalSize) {
  uint32_t dataSize = SAMPLE_RATE * RECORD_TIME * 2;
  *totalSize = sizeof(WavHeader) + dataSize;
  
  if (ESP.getFreeHeap() < *totalSize + 20000) { 
    updateDisplay(0, "Memory Full"); 
    return false; 
  }
  Serial.printf(C_YELLOW "\r\n[Voice] 🎤 녹음 시작 (2초)...\r\n" C_RESET);
  
  // 일반 malloc 대신 단편화가 적은 8-bit Internal RAM 영역 명시적 할당
  *audioBuffer = (uint8_t*)heap_caps_malloc(*totalSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL); 
  if (*audioBuffer == NULL) return false;
  
  WavHeader header; memcpy(header.riff, "RIFF", 4); header.overall_size = *totalSize - 8; 
  memcpy(header.wave, "WAVE", 4); memcpy(header.fmt_chunk_marker, "fmt ", 4); 
  header.length_of_fmt = 16; header.format_type = 1; header.channels = 1; 
  header.sample_rate = SAMPLE_RATE; header.byterate = SAMPLE_RATE * 2; 
  header.block_align = 2; header.bits_per_sample = 16; 
  memcpy(header.data_chunk_header, "data", 4); header.data_size = dataSize; 
  memcpy(*audioBuffer, &header, sizeof(WavHeader));
  
  size_t bytesRead = 0; 
  i2s_read(I2S_PORT, (void*)(*audioBuffer + sizeof(WavHeader)), dataSize, &bytesRead, portMAX_DELAY);
  Serial.println(C_GREEN "→ ✅ 필터링 완료" C_RESET); 
  return true;
}

void executeVoiceCommand(String jsonResponse) {
  JsonDocument doc; if (deserializeJson(doc, jsonResponse)) { Serial.println(C_RED "⚠️ JSON 파싱 실패" C_RESET); return; }
  String transcript = doc["transcript"] | "인식 실패"; String resultText = doc["result_text"] | "명령 수신";
  int cmd = doc["spray"] | -1; int dur = doc["duration"] | 3; int music = doc["music"] | 0;
  Serial.printf("\r\n" C_CYAN "[Voice Result]" C_RESET "\r\n 🗣️  인식: %s\r\n 🤖  서버: %s\r\n", transcript.c_str(), resultText.c_str());
  if (cmd >= 1 && cmd <= 4) triggerSpray(cmd, dur, music, resultText, false);
  else if (cmd == 0) { stopSystem(); updateDisplay(0, resultText); Serial.println(C_YELLOW "🛑 시스템 정지" C_RESET); }
}

// --- 최적화 3: HTTP 응답 데이터 메모리 예약 ---
bool sendAudioToServer(uint8_t* audioBuffer, uint32_t totalSize, String& response) {
  if (isCommunicate) { Serial.println(C_RED "→ ⚠️ Lock" C_RESET); return false; }
  isCommunicate = true; Serial.println(C_GREEN "→ 🗣️ 전송 중..." C_RESET); updateDisplay(0, "Thinking...");
  esp_task_wdt_delete(NULL); bool success = false; initNetworkSession();
  HTTPClient http; http.setTimeout(15000); 
  
  if (http.begin(sharedClient, serverName)) {
    http.addHeader("Content-Type", "audio/wav");
    int httpCode = http.POST(audioBuffer, totalSize);
    if (httpCode > 0) { 
      response.reserve(512); // 대용량 응답 문자열 사전 할당
      response = http.getString(); 
      Serial.println(C_GREEN "→ ✅ 응답 수신 완료" C_RESET); 
      success = true; 
    } 
    else Serial.printf(C_RED "→ ❌ HTTP %d\r\n" C_RESET, httpCode);
    http.end();
  }
  esp_task_wdt_add(NULL); isCommunicate = false; return success;
}

void recordAndSendVoice() {
  if (WiFi.status() != WL_CONNECTED) return;
  SystemMode prevMode = currentMode; currentMode = MODE_VOICE; 
  uint8_t* audioBuffer = NULL; uint32_t totalSize = 0;
  if (recordAudio(&audioBuffer, &totalSize)) {
    String response = "";
    if (sendAudioToServer(audioBuffer, totalSize, response)) executeVoiceCommand(response);
  }
  if (audioBuffer != NULL) free(audioBuffer);
  currentMode = (prevMode == MODE_VOICE) ? MODE_READY : prevMode; showPrompt();
}

void autoWeatherScheduler() {
  if (currentMode != MODE_WEATHER || isRunning) return; 
  if (millis() - lastWeatherCallMillis >= WEATHER_INTERVAL) {
    lastWeatherCallMillis = millis();
    sendServerRequest(buildWeatherRequestPayload(lastWeatherRegion));
  }
}

void handleWebClient() {
  WiFiClient client = webServer.available(); if (!client) return;
  client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n"); client.stop();
}

void showPrompt() { Serial.print("\r\033[K" C_YELLOW "👉 명령 입력 >>" C_RESET); }
void printMainMenu() { Serial.println("\r\n[1]수동 [2]날씨 [3]설정 [4]데모 [5]음성 [6]파형 [7]반응 [8]정보 [9]LED [10]소음"); showPrompt(); }
void redrawInputLine(String &buffer) { Serial.print("\r\033[K" C_YELLOW "👉 명령 입력 >>" C_RESET); Serial.print(buffer); }
void printDashboard() { Serial.printf("\r\nWiFi: %d dBm | Weights: %.1f %.1f %.1f %.1f\n", WiFi.RSSI(), weights[0], weights[1], weights[2], weights[3]); printMainMenu(); }

void changeVolume(int vol) { 
  currentVolume = constrain(vol, 0, 30); 
  myDFPlayer.volume(currentVolume); 
  delay(40); // DFPlayer 연속 시리얼 통신으로 인한 Lock-up 방지
  
  volumeNeedsSave = true;
  lastVolumeChangeTime = millis();
  
  Serial.printf("\r\033[K" C_GREEN "🔊 볼륨: %d\r\n" C_RESET, currentVolume); 
  showPrompt(); 
}

void initOTA() { 
  ArduinoOTA.setHostname("SmartDiffuser"); 
  ArduinoOTA.setPassword("1234"); 
  
  ArduinoOTA.onStart([]() {
    esp_task_wdt_delete(NULL); // 업데이트 시작: 감시견 취침
    forceAllOff(); 
    updateDisplay(0, "OTA Updating...");
    Serial.println("\r\n[OTA] 무선 업데이트 시작...");
  });
  
  ArduinoOTA.onEnd([]() {
    updateDisplay(0, "Update Success!");
    Serial.println("\r\n[OTA] 무선 업데이트 완료!");
    esp_task_wdt_add(NULL); // 완료 시 감시견 복구 (안전을 위해)
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = (progress / (total / 100));
    updateDisplay(0, String("OTA ") + String(percent) + "%");
    Serial.printf("[OTA] 진행률: %u%%\r", percent);
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    updateDisplay(0, "OTA Error!");
    Serial.printf("\r\n[OTA] 에러 발생 코드: [%u]\r\n", error);
    
    // ★ 최종 수정: 에러 발생 시 감시견을 어설프게 건드리지 않고, 1.5초 뒤 깔끔하게 재부팅!
    delay(1500);
    ESP.restart(); 
  });
  
  ArduinoOTA.begin(); 
}

void handleOTA() { ArduinoOTA.handle(); }
