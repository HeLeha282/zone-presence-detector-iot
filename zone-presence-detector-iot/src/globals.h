#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include "Ld2450.h"

// ===================================================================
//  ОБЩИЕ ОБЪЕКТЫ (определены в main.cpp)
//  Нужны в нескольких модулях: MQTT-обработчик, зоны и т.д.
// ===================================================================
extern WiFiClient   espClient;
extern PubSubClient client;
extern Ld2450       ld2450;
