#pragma once
#include <Arduino.h>
#include <PubSubClient.h>

// ===================================================================
//  МОДУЛЬ MQTT: обработка входящих команд + переподключение
// ===================================================================

// Колбэк входящих сообщений (команды take_photo / add_zone / ...)
void callback(char *topic, byte *payload, unsigned int length);

// Переподключение к брокеру + подписка на топик команд
void reconnect();
