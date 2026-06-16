#pragma once
#include <Arduino.h>
#include "esp_camera.h"

// ===================================================================
//  МОДУЛЬ КАМЕРЫ: инициализация, переключение режимов, отправка фото
// ===================================================================

// Состояние камеры
extern bool cameraIsHighMode;
extern bool cameraInitialized;

// Инициализация (стартует в LOW режиме)
void initCamera();

// Конфиг/настройки сенсора
camera_config_t createCameraConfig(int xclk_hz, int framesize, int quality,
                                    int fb_count, int grab_mode);
void applySensorSettingsFull(bool highMode);
bool applySensorSettings(int framesize, int quality, bool highMode); // on-the-fly (не используется, оставлено)

// Переключение режимов (полный реинит)
void setCameraHighMode();
void setCameraLowMode();

// Отправка фото
bool sendPhotoSimple(String message); // рабочий вариант (HIGH -> снять -> LOW)
bool sendPhotoToServer();             // простой бинарный POST (оставлено)
