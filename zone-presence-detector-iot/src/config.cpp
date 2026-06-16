#include "config.h"

// ---------------- WiFi ----------------
const char *WIFI_SSID = "B5";
const char *WIFI_PASSWORD = "01234567890";

// ---------------- MQTT ----------------
const char *MQTT_SERVER = "45.39.190.176"; // old - 104.252.89.123
const int MQTT_PORT = 1883;
const int ASSISTANT_ID = 1;

// ---------------- Сервер загрузки фото ----------------
const char *PHOTO_SERVER_URL =
    "http://45.39.190.176:8080/api/management_assistant/upload-photo";

// ---------------- SmartThings ----------------
const unsigned long ZONE_LEAVE_TIMEOUT = 1500; // мс антидребезга выхода из зоны
