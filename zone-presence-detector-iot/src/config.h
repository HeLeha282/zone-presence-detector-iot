#pragma once
#include "esp_camera.h"

// ===================================================================
//  ВСЕ НАСТРОЙКИ И СЕКРЕТЫ ПРОЕКТА В ОДНОМ МЕСТЕ
//  Значения лежат в config.cpp
// ===================================================================

// ---------------- WiFi ----------------
extern const char *WIFI_SSID;
extern const char *WIFI_PASSWORD;

// ---------------- MQTT ----------------
extern const char *MQTT_SERVER;
extern const int   MQTT_PORT;
extern const int   ASSISTANT_ID; // раньше переменная id

// ---------------- Сервер загрузки фото ----------------
extern const char *PHOTO_SERVER_URL;

// ---------------- SmartThings ----------------
extern const unsigned long ZONE_LEAVE_TIMEOUT; // мс антидребезга выхода из зоны

// ===================================================================
//  КАМЕРА — ПИНЫ (AI-THINKER ESP32-CAM, твоя распиновка)
// ===================================================================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5

#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// ===================================================================
//  КАМЕРА — РЕЖИМЫ (итоговые рабочие значения, дубли убраны)
// ===================================================================

// HIGH — съёмка фото
#define HIGH_XCLK_HZ   20000000        // 20 MHz
#define HIGH_FRAMESIZE FRAMESIZE_UXGA  // 1600x1200
#define HIGH_QUALITY   8               // 0-63, меньше = лучше
#define HIGH_FB_COUNT  2
#define HIGH_GRAB_MODE CAMERA_GRAB_LATEST

// LOW — дежурный режим (экономия)
#define LOW_XCLK_HZ    5000000         // 5 MHz
#define LOW_FRAMESIZE  FRAMESIZE_SVGA  // 800x600
#define LOW_QUALITY    30
#define LOW_FB_COUNT   1
#define LOW_GRAB_MODE  CAMERA_GRAB_LATEST
