#ifndef GLOBALS_H
#define GLOBALS_H

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
// [1] 하드웨어 핀 및 상수 설정
// ==========================================
#define NUM_LEDS 16
const int PIN_LED = 2;
const int PIN_SUNNY  = 4;
const int PIN_CLOUDY = 13;
const int PIN_RAIN   = 14;
const int PIN_SNOW   = 27;

const int LOADCELL_DT[4]  = {32, 39, 36, 34};
const int LOADCELL_SCK[4] = {33, 33, 33, 33};

const int DFPLAYER_RX_PIN = 25; 
const int DFPLAYER_TX_PIN = 26;
const int PIN_BUSY = 21;

const int NEXTION_TX_PIN = 17;  
const int NEXTION_RX_PIN = 16;

#define I2S_WS   19
#define I2S_SD   35
#define I2S_SCK  22
#define I2S_PORT I2S_NUM_0

// ==========================================
// [2] 시스템 전역 설정
// ==========================================
#define WDT_TIMEOUT 60
#define WDT_YIELD_TIME_MS   (1 / portTICK_PERIOD_MS)
#define I2S_READ_TIMEOUT (100 / portTICK_PERIOD_MS)
#define SAMPLE_RATE 16000
#define RECORD_TIME 2
#define CLEANING_INTERVAL 86400000

namespace Config {
    const int SPRAY_PINS[4] = {27, 14, 13, 4}; 
    const int PIN_LED = 2;
    const float EMPTY_WEIGHT = 19.8f;
    const unsigned long AMBIENT_COOL_DOWN = 5000; 
    const unsigned long SCENT_PURGE_COOLTIME = 15 * 60 * 1000; 
    const unsigned long ONE_HOUR_MS = 60 * 60 * 1000; 
    const int HOURLY_BUFFER_SIZE = 20; 
}

#define C_RESET   "\033[0m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN    "\033[36m"
#define C_BOLD    "\033[1m"

// ==========================================
// [3] 데이터 구조체 및 열거형
// ==========================================
enum SystemMode { MODE_READY = 0, MODE_MANUAL, MODE_WEATHER, MODE_SETTING, MODE_DEMO, MODE_VOICE, MODE_VISUAL, MODE_REACTIVE, MODE_DASHBOARD, MODE_SLEEP, MODE_AMBIENT = 10, MODE_LED };
struct WavHeader { char riff[4]; uint32_t overall_size; char wave[4]; char fmt_chunk_marker[4]; uint32_t length_of_fmt; uint16_t format_type; uint16_t channels; uint32_t sample_rate; uint32_t byterate; uint16_t block_align; uint16_t bits_per_sample; char data_chunk_header[4]; uint32_t data_size; };
enum LedMode { LED_SOLID = 0, LED_BREATHE, LED_RAINBOW, LED_MUSIC };

// ==========================================
// [4] 전역 객체
// ==========================================
extern HardwareSerial mySoftwareSerial;
extern HardwareSerial nexSerial;
extern DFRobotDFPlayerMini myDFPlayer;
extern HX711 scales[4];
extern Preferences prefs;
extern WiFiServer webServer;
extern Adafruit_NeoPixel strip;

extern TaskHandle_t SensorTaskHandle;
extern TaskHandle_t NetworkTaskHandle;
extern QueueHandle_t audioEventQueue;
extern QueueHandle_t networkQueue;
extern EventGroupHandle_t networkEventGroup;

// ==========================================
// [5] 전역 변수
// ==========================================
extern SystemMode currentMode;
extern String deviceId;
extern bool isRunning;
extern bool isCommunicate;
extern unsigned long lastActivityTime;
extern const char* ssid;
extern const char* password;
extern String serverName;
extern unsigned long wifiRetryInterval;
extern unsigned long lastWifiRetryMillis;

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

extern float weights[4];
extern float maxWeight;
extern float calibration_factor;
extern unsigned long lastWeightCheckTime;
extern const float WEIGHT_THRESHOLD;
extern volatile bool hxReady[4];

extern bool blendModeEnabled;
extern String blendSelection;
extern bool blendSprayActive;

extern unsigned long lastWeatherCallMillis;
extern const unsigned long WEATHER_INTERVAL;
extern String lastWeatherRegion;
extern int lastWeatherIconId;
extern String lastWeatherLabel;

extern unsigned long lastNozzleSprayTime[4]; 
extern bool schedulerEnabled;
extern int activeStartHour;  
extern int activeEndHour;    
extern unsigned long lastPollTime;
extern const unsigned long POLL_INTERVAL;
extern int musicMapping[4];

extern String slotPlaylists[4];
extern int currentSlotTracks[10];
extern int currentSlotTracksCount;
extern int currentPlaylistIdx;

extern int soundThreshold;
extern int currentDbLevel; 
extern unsigned long lastReactionTime;
extern int ambientCycleCount;
extern int ambientCycleStorage[5];
extern bool isFirstAmbientRun;
extern int lastAmbientScent;
extern int targetAmbientScent;
extern bool isWaitingForTestDb;
extern unsigned long lastAmbientTime;
extern int currentAmbientTrack;
extern bool forceAmbientSkip;
extern float dbHistory[20];
extern int dbHistoryIndex;
extern int dbHistoryCount;
extern unsigned long lastScentChangeTime;
extern unsigned long last1HourCheckTime;

extern String lastWebMessage;
extern String inputBuffer;
extern int demoStep;
extern unsigned long prevDemoMillis;
extern unsigned long prevMotorMillis;
extern unsigned long startTimeMillis;

// ==========================================
// [6] 디스플레이(Nextion) 관련 상태 및 페이지 상수
// ==========================================
#define PAGE_WEATHER_OFF 0
#define PAGE_MODE_SELECT 1
#define PAGE_WEATHER 2
#define PAGE_MANUAL 3
#define PAGE_MANUAL_SCENT_BASE 4
#define PAGE_BLEND_HOME 9
#define PAGE_WIFI_RESET 20
#define PAGE_DEVICE_STATUS 21
#define PAGE_TARE 22
#define PAGE_LED_ON 23
#define PAGE_LED_OFF 24
#define PAGE_LED_DIM 25
#define PAGE_LED_NORMAL 26
#define PAGE_LED_BRIGHT 27
#define PAGE_INTENSITY_HOME 28
#define PAGE_OFFLINE 29
#define PAGE_INTENSITY_LOW 30
#define PAGE_INTENSITY_MEDIUM 31
#define PAGE_INTENSITY_HIGH 32
#define PAGE_STARTUP_HOME 33
#define PAGE_LOW_FLUID 80

extern bool hasWeatherSnapshot;
extern bool hasTempHumiSnapshot;
extern float lastWeatherTempC;
extern float lastWeatherHumi;
extern int currentDisplayPage;
extern unsigned long manualModeOffMillis;
extern int lastStoppedManualScent;
extern bool offlineModeActive;
extern String pendingManualScent;

// ==========================================
// [7] 전역 함수 선언
// ==========================================
// --- DisplayController.cpp ---
void clearNextionInputBuffer();
void showPage(int pageId);
void showStartupReadyPage();
void showLowFluidPage(int cartNum = -1);
void checkNextionInput();
void handleNextionCmd(const String &cmd);
void nexSend(const String &cmd);
void updateDisplay(int iconID, String text);
void updateClockDisplay();
void updateTempHumi(float tempC, float humi);
void updateProgressBar(int val);
void updateProgressBar(int barIndex, int val);
void updateScentProgressBars();
void refreshWeatherFieldsIfVisible();
void showWeatherPageForRefreshResponse(bool isWeatherRefreshResponse);
void applyServerVolumeWithoutPageChange(int volumeValue);
void showWeatherPageByState();
void showWeatherOffPage();
void showManualPageByState();
void clearWeatherState();
void beginWeatherRefresh();
void showManualPageForServerScent(int activeScent);
String processNextionChar(char c, String &buf, bool executeCmd);
int weatherIconFromText(const String &weatherText);
bool hasValidWeatherText(const String &weatherText);
float calculateScentPercent(float weightValue);
bool isBlendSelectionInProgress();
bool isValidBlendCommand(int activeScent);
void setOfflineModeActive(bool active);
void showPrompt();
void printMainMenu();
void printSettingMenu();
void printManualMenu();
void redrawInputLine(String &buffer);
void printDashboard();
void changeVolume(int vol);

// --- NetworkUI.cpp ---
void markLocalStop();
void clearLocalStopMark();
void markLocalWeatherStart();
void markLocalSettingsChanged();
void connectWiFi();
void manageWiFi();
void pollServer();
void sendServerRequest(String payload);
void requestWeatherRefresh(const String &region);
void rememberWeatherRegion(const String &region);
void handleWebClient();
void autoWeatherScheduler();
void initOTA();
void handleOTA();
void networkTaskLoop(void *pvParameters);
void recordAndSendVoice();

// --- SystemLogic / Hardware ---
void initSystem();
void runSystem();
void setSystemMode(SystemMode mode, String msg = "");
void wakeUpSystem();
void triggerSpray(int cmd, int dur, int music, String txt, bool isWeatherMode = false);
void checkSensorHealth();
void runScheduler(); 
void runAutoCleaning(); 
void resetScaleZero();
void SprayIntensity(int intensityVal);
String getTrackName(int trackNum);
void updateMusicMapping(String data);
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
void resetWeightFilters();
void printCalibrationInfo();
void runAmbientMode();
void setLedColor(uint8_t r, uint8_t g, uint8_t b);
float calculateStdDev(int* samples, int count, float average); 
void applyNozzleHardwareState(); 
int getPinFromCommand(int cmd);
int parseAndSetNozzles(String cmdStr);
void handleInput(String input);
void checkSerialInput();
void runManualMode(String input);
void enterWeatherMode(bool runNow);
void runSprayLogic();
void runAutoDemoLoop();
void checkSafety();

#endif
