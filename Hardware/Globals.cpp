#include "Globals.h"

// ==========================================
// [5] 전역 객체 (Global Objects)
// ==========================================
HardwareSerial mySoftwareSerial(2);
HardwareSerial nexSerial(1);
DFRobotDFPlayerMini myDFPlayer;
HX711 scales[4];
Preferences prefs;
WiFiServer webServer(80);
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);

// FreeRTOS 핸들러 초기화
TaskHandle_t SensorTaskHandle = NULL;
TaskHandle_t NetworkTaskHandle = NULL;
QueueHandle_t audioEventQueue = NULL;
QueueHandle_t networkQueue = NULL;
EventGroupHandle_t networkEventGroup = NULL;

// ==========================================
// [6] 전역 변수 (Global Variables)
// ==========================================
// --- 시스템 상태 ---
SystemMode currentMode = MODE_READY; 
String deviceId = "SS";
bool isRunning = false;
bool isCommunicate = false; 
unsigned long lastActivityTime = 0;

// --- 네트워크 ---
const char* ssid      = "Jaehong_WiFi";
const char* password  = "12345678";
String serverName     = "https://tgrwszo3iwurntqeq76s5rro640asnwq.lambda-url.ap-northeast-2.on.aws/";
unsigned long wifiRetryInterval = 5000;
unsigned long lastWifiRetryMillis = 0;

// --- 하드웨어 제어 (센서, 모터, LED, 볼륨) ---
bool isSensorOk[4] = {true, true, true, true};
bool isAudioOk = true;
bool activeNozzles[4] = {false, false, false, false};
bool isSpraying   = false;
unsigned long sprayDuration = 3000;
long REST_TIME        = 6000;
int currentIntensity  = 2;
const long MAX_RUN_TIME     = 270000;
int currentVolume = 15;

uint8_t ledR = 255; 
uint8_t ledG = 255; 
uint8_t ledB = 255; 
int ledBrightness = 150;
bool ledEnabled = true;

LedMode ledEffect = LED_SOLID;

// --- 향수 및 무게 데이터 ---
float weights[4] = {0.0, 0.0, 0.0, 0.0};
float maxWeight = 95.4; 
float calibration_factor = 430.0;
unsigned long lastWeightCheckTime = 0;
const float WEIGHT_THRESHOLD = 5.0;
volatile bool hxReady[4] = {false, false, false, false};

// --- 블렌딩 모드 ---
bool blendModeEnabled = false;
String blendSelection = "";
bool blendSprayActive = false;

// --- 날씨 모드 ---
unsigned long lastWeatherCallMillis = 0;
const unsigned long WEATHER_INTERVAL = 3600000;
String lastWeatherRegion = "";
int lastWeatherIconId = 0;
String lastWeatherLabel = "";

// --- 타이머 및 스케줄러 & 음악 플레이리스트 ---
unsigned long lastNozzleSprayTime[4] = {0, 0, 0, 0};
bool schedulerEnabled = true;
int activeStartHour   = 8; 
int activeEndHour     = 23;
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000;
int musicMapping[4] = {1, 6, 11, 26}; // 기본값

// ★ [추가] 슬롯별 플레이리스트 및 바통 터치 변수 정의
String slotPlaylists[4] = {"1,2,3", "6,7,8", "11,12,13", "26"};
int currentSlotTracks[10] = {26,0};
int currentSlotTracksCount = 1;
int currentPlaylistIdx = 0;

String getTrackName(int trackNum) {
  if (trackNum <= 0) return "재생 안 함";
  return String(trackNum) + "번 트랙"; // 예: "26번 트랙", "27번 트랙"
}

// --- 오디오 및 앰비언트 모드 ---
int soundThreshold = 75;
int currentDbLevel = 0; 
unsigned long lastReactionTime = 0;
int ambientCycleCount = 0;
int ambientCycleStorage[5] = {0, 0, 0, 0, 0};
bool isFirstAmbientRun = true;
int lastAmbientScent = 0;
int targetAmbientScent = 0;
bool isWaitingForTestDb = false;
unsigned long lastAmbientTime = 0;    
int currentAmbientTrack = 0;
bool forceAmbientSkip = false;

float dbHistory[20] = {0.0f};
int dbHistoryIndex = 0;
int dbHistoryCount = 0;
unsigned long lastScentChangeTime = 0;
unsigned long last1HourCheckTime = 0;

// --- 기타 (데모, 터미널) ---
String lastWebMessage = "Ready";
String inputBuffer    = "";
int demoStep = 0;
unsigned long prevDemoMillis  = 0;
unsigned long prevMotorMillis = 0;
unsigned long startTimeMillis = 0;

// ==========================================
// [7] 공통 헬퍼 함수
// ==========================================
void SprayIntensity(int intensityVal) {
    currentIntensity = constrain(intensityVal, 1, 3);
    if (currentIntensity == 1) REST_TIME = 9000;      
    else if (currentIntensity == 2) REST_TIME = 6000; 
    else REST_TIME = 5000;

    prefs.putInt("intensity", currentIntensity);
}

int getPinFromCommand(int cmd) {
    if (cmd >= 1 && cmd <= 4) return Config::SPRAY_PINS[cmd - 1];
    return -1;
}
