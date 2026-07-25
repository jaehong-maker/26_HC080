/*
 * [프로젝트] 스마트 디퓨저 (Smart Diffuser) - 모듈화 버전
 * [작성자] 21학번 류재홍
 */

#include "Globals.h"

void setup() {
  Serial.begin(115200);
  
  // 99번 쳤을 때 9600에서 반응했다면 9600으로, 115200에서 반응했다면 115200으로 적어주세요!
  // 보통 9600일 확률이 99%입니다.
  // Nextion is initialized as nexSerial in initSystem().
  // UART2 is reserved for DFPlayer on pins 25/26.
  
  initSystem();
}

void loop() {
  runSystem();  // 메인 루프 (SystemLogic.cpp 에 정의)
}
