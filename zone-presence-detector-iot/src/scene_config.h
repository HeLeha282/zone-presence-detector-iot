#pragma once
#include <Arduino.h>
#include <string>

// ===================================================================
//  SCENE CONFIG: runtime-таблицы токенов/сцен/привязок + движок
//  присутствия. Всё хранится в RAM, при изменении пишется в NVS.
// ===================================================================

// Нормализация события ("exit"/"out"/"leaving" → "leave", "in"/"entering" → "enter")
// Экспортируется для использования в MQTT callback при формировании ответа
std::string normalizeZoneEvent(const char *event);

// --- Токены ---
void addToken(const char *tokenId, const char *token);
void removeToken(const char *tokenId);

// --- Сцены ---
void addScene(const char *sceneKey, const char *sceneId, const char *tokenId);
void removeScene(const char *sceneKey);

// --- Привязки зон ---
// delayMs: сколько мс ждать после обнаружения перехода перед срабатыванием (0 = немедленно)
// Для "enter": если человек ушёл до истечения delay — сцена НЕ сработает
// Для "leave": срабатывает через delay после подтверждённого выхода
void bindZone(const char *zoneName, const char *event, const char *sceneKey,
              unsigned long delayMs = 0);
void unbindZone(const char *zoneName, const char *event, const char *sceneKey);

// --- Движок присутствия ---
// Вызывать каждый кадр с JSON от ld2450.getDataFromSensorInJSON() и millis()
void checkZonesPresence(const std::string &json, unsigned long now);

// --- Дамп конфига в JSON (для get_config) ---
std::string getConfigAsJson();

// --- Persistence (NVS / Preferences) ---
void loadSceneConfig(); // вызвать в setup() после Serial.begin()
void saveSceneConfig(); // вызывается автоматически при каждом изменении
