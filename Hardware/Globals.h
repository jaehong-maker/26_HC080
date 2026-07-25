#ifndef GLOBALS_H
#define GLOBALS_H

// ==========================================
// [1] 필수 라이브러리 (Libraries)
// ==========================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "HX711.h"
#include <Preferences.h>
#include "DFRobotDFPlayerMini.h"
#include <esp_task_wdt.h>
#include <driver/i2s.h>
#include <math.h>
#include <ArduinoOTA.h>
#include <time.h> 
#include <freertos/queue.h> 
#include <WiFiManager.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>

// ==========================================
// [2] 하드웨어 핀 및 상수 설정 (Pins & Constants)
// ==========================================
// 네오픽셀 (LED)
#define NUM_LEDS 16
const int PIN_LED = 2;

// 분사 모터 (노즐) 및 펌프
const int PIN_SUNNY  = 4;
const int PIN_CLOUDY = 13;
const int PIN_RAIN   = 14;
const int PIN_SNOW   = 27;

// 로드셀 (무게 센서) 1~4
const int LOADCELL_DT[4]  = {32, 39, 36, 34};
const int LOADCELL_SCK[4] = {33, 33, 33, 33};

// DFPlayer (오디오 스피커)
const int DFPLAYER_RX_PIN = 25; 
const int DFPLAYER_TX_PIN = 26;

// Nextion (디스플레이)
const int NEXTION_TX_PIN = 17;  
const int NEXTION_RX_PIN = 16;

// I2S 마이크
#define I2S_WS   19
#define I2S_SD   35
#define I2S_SCK  22
#define I2S_PORT I2S_NUM_0

// ==========================================
// [3] 시스템 전역 설정 (System Config)
// ==========================================
#define WDT_TIMEOUT 60
#define WDT_YIELD_TIME_MS   (1 / portTICK_PERIOD_MS)
#define I2S_READ_TIMEOUT (100 / portTICK_PERIOD_MS)
#define SAMPLE_RATE 16000
#define RECORD_TIME 2
#define CLEANING_INTERVAL 86400000 // <--- ★ 여기에 딱 한 줄 추가!

namespace Config {
    const int SPRAY_PINS[4] = {27, 14, 13, 4}; 
    const int PIN_LED = 2;
    const int PIN_DF_BUSY = 21;
    const float EMPTY_WEIGHT = 19.8f;
    const unsigned long AMBIENT_COOL_DOWN = 5000; 
}

// 터미널 색상 출력용 매크로
#define C_RESET   "\033[0m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"

// ==========================================
// [4] 데이터 구조체 및 열거형 (Structs & Enums)
// ==========================================
enum SystemMode {
  MODE_READY = 0,
  MODE_MANUAL,    // 1
  MODE_WEATHER,   // 2
  MODE_SETTING,   // 3
  MODE_DEMO,      // 4
  MODE_VOICE,     // 5  <--- ★ 이 줄을 다시 추가해 주세요!
  MODE_VISUAL,    // 6
  MODE_REACTIVE,  // 7
  MODE_DASHBOARD, // 8
  MODE_SLEEP,     // 9
  MODE_AMBIENT = 10,
  MODE_LED        // 11
};

struct WavHeader {
  char riff[4]; uint32_t overall_size; char wave[4];
  char fmt_chunk_marker[4]; uint32_t length_of_fmt; uint16_t format_type; uint16_t channels;
  uint32_t sample_rate; uint32_t byterate; uint16_t block_align; uint16_t bits_per_sample;
  char data_chunk_header[4]; uint32_t data_size;
};

enum LedMode {
  LED_SOLID = 0,
  LED_BREATHE,
  LED_RAINBOW,
  LED_MUSIC
};

// ==========================================
// [5] 전역 객체 (Global Objects)
// ==========================================
extern HardwareSerial mySoftwareSerial;
extern HardwareSerial nexSerial;
extern DFRobotDFPlayerMini myDFPlayer;
extern HX711 scales[4];
extern Preferences prefs;
extern WiFiServer webServer;
extern Adafruit_NeoPixel strip;

// FreeRTOS 핸들러
extern TaskHandle_t SensorTaskHandle;
extern TaskHandle_t NetworkTaskHandle;
extern QueueHandle_t audioEventQueue;
extern QueueHandle_t networkQueue;
extern EventGroupHandle_t networkEventGroup;

// ==========================================
// [6] 전역 변수 (Global Variables)
// ==========================================
// 시스템 상태
extern SystemMode currentMode;
extern String deviceId;
extern bool isRunning;
extern bool isCommunicate;
extern unsigned long lastActivityTime;

// 네트워크
extern const char* ssid;
extern const char* password;
extern String serverName;
extern unsigned long wifiRetryInterval;
extern unsigned long lastWifiRetryMillis;

// 하드웨어 제어 (센서, 모터, LED, 볼륨)
extern bool isSensorOk[4]; 
extern bool isAudioOk;
extern bool activeNozzles[4];
extern bool isSpraying;
extern unsigned long sprayDuration;
extern long REST_TIME;
extern int currentIntensity;
extern const long MAX_RUN_TIME;
extern int currentVolume;
extern uint8_t ledR, ledG, ledB; 
extern int ledBrightness;
extern bool ledEnabled;
extern LedMode ledEffect;

// 향수 및 무게 데이터
extern float weights[4];
extern float maxWeight;
extern float calibration_factor;
extern unsigned long lastWeightCheckTime;
extern const float WEIGHT_THRESHOLD;
extern volatile bool hxReady[4];

// 블렌딩 모드
extern bool blendModeEnabled;
extern String blendSelection;
extern bool blendSprayActive;

// 날씨 모드
extern unsigned long lastWeatherCallMillis;
extern const unsigned long WEATHER_INTERVAL;
extern String lastWeatherRegion;
extern int lastWeatherIconId;
extern String lastWeatherLabel;

// 타이머 및 스케줄러
extern unsigned long lastNozzleSprayTime[4]; 
extern bool schedulerEnabled;
extern int activeStartHour;  
extern int activeEndHour;    
extern unsigned long lastPollTime;
extern const unsigned long POLL_INTERVAL;
extern int musicMapping[4]; // <--- ★ 여기에 한 줄 추가!

// 오디오 및 앰비언트 모드
extern int soundThreshold;
extern int currentDbLevel; 
extern unsigned long lastReactionTime;
extern int ambientCycleCount;
extern int ambientCycleStorage[5];
extern bool isFirstAmbientRun;
extern int lastAmbientScent;
extern bool isWaitingForTestDb;
extern unsigned long lastAmbientTime;
extern int currentAmbientTrack;
extern bool forceAmbientSkip;
extern float dbHistory[20];
extern int dbHistoryIndex;
extern int dbHistoryCount;

// 기타 (데모, 터미널)
extern String lastWebMessage;
extern String inputBuffer;
extern int demoStep;
extern unsigned long prevDemoMillis;
extern unsigned long prevMotorMillis;
extern unsigned long startTimeMillis;

// ==========================================
// [7] 전역 함수 선언 (Global Functions)
// ==========================================
// 📄 SystemLogic.cpp
void initSystem();
void runSystem();
void setSystemMode(SystemMode mode, String msg = "");
void wakeUpSystem();
void triggerSpray(int cmd, int dur, int music, String txt, bool isWeatherMode = false);
void checkSensorHealth();
void runScheduler(); 
void runAutoCleaning(); 
void resetScaleZero();
float calculateScentPercent(float currentWeight); // <--- void를 float으로 변경!
void SprayIntensity(int intensityVal);
String getTrackName(int trackNum); // <--- 추가
void updateMusicMapping(String data); // <--- 추가

// 📄 Hardware.cpp
void initMicrophone();
void runSoundVisualizer();
void runSoundReaction();
void checkBackgroundNoise();
void sensorTaskLoop(void *parameter);
void forceAllOff();
void stopSystem();
void resetManualState();
void playSound(int trackNum);
void bootAnimation();
void systemHeartbeat();
void monitorWeight();
void resetWeightFilters(); // 이 줄을 추가하세요!
void printCalibrationInfo();
void runAmbientMode();
void setLedColor(uint8_t r, uint8_t g, uint8_t b);
float calculateStdDev(int* samples, int count, float average); 
void applyNozzleHardwareState(); 

// 📄 NetworkUI.cpp
void connectWiFi();
void manageWiFi();
void pollServer();
void sendServerRequest(String payload);
void rememberWeatherRegion(const String &region);
String buildWeatherRequestPayload(const String &region);
void handleWebClient();
void autoWeatherScheduler();
// 음성 인식 관련 기능 제거됨
void checkNextionInput();
void clearNextionInputBuffer();
void clearNextionInputBuffer();
void handleNextionCmd(const String &cmd);
void nexSend(const String &cmd);
void updateDisplay(int iconID, String text);
void updateClockDisplay();
void updateTempHumi(float tempC, float humi);
void updateProgressBar(int val);
void updateProgressBar(int barIndex, int val);
void updateScentProgressBars();
void showStartupReadyPage();
void showLowFluidPage(int cartNum = -1);
void showPrompt();
void printMainMenu();
void printSettingMenu();
void printManualMenu(); 
void redrawInputLine(String &buffer);
void printDashboard();
void changeVolume(int vol);
void initOTA();
void handleOTA();
int getPinFromCommand(int cmd);
void networkTaskLoop(void *pvParameters);

#define PIN_BUSY 21

int parseAndSetNozzles(String cmdStr);
void handleInput(String input);
void checkSerialInput();
void runManualMode(String input);
void enterWeatherMode(bool runNow);
void runSprayLogic();
void runAutoDemoLoop();
void checkSafety();

#endif
