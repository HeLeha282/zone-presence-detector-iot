#include "scene_config.h"
#include "config.h"
#include "smartthings.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

// ===================================================================
//  Внутренние структуры
// ===================================================================

struct SceneEntry {
  std::string sceneId;
  std::string tokenId;
};

struct Binding {
  std::string   zoneName;
  std::string   event;     // "enter" или "leave" (нормализовано)
  std::string   sceneKey;
  unsigned long delayMs;   // 0 = немедленно
};

struct ZoneState {
  bool          wasInside = false;
  unsigned long lastSeen  = 0;
};

// Отложенный вызов: зафиксированное событие ждёт своего времени
struct PendingFire {
  std::string   zoneName;
  std::string   event;
  std::string   sceneKey;
  unsigned long fireAt; // millis() момент срабатывания
};

static std::map<std::string, std::string> s_tokens;
static std::map<std::string, SceneEntry>  s_scenes;
static std::vector<Binding>               s_bindings;
static std::map<std::string, ZoneState>   s_zoneStates;
static std::vector<PendingFire>           s_pendingFires;

// ===================================================================
//  Вспомогательные функции
// ===================================================================

std::string normalizeZoneEvent(const char *ev)
{
  std::string e(ev);
  if (e == "exit" || e == "out" || e == "leaving") return "leave";
  if (e == "in"   || e == "entering")               return "enter";
  return e;
}

static void fireScene(const std::string &sceneKey)
{
  auto scIt = s_scenes.find(sceneKey);
  if (scIt == s_scenes.end())
  {
    Serial.printf("[SC] Unknown scene key: %s\n", sceneKey.c_str());
    return;
  }
  auto tokIt = s_tokens.find(scIt->second.tokenId);
  if (tokIt == s_tokens.end())
  {
    Serial.printf("[SC] Unknown token id: %s\n", scIt->second.tokenId.c_str());
    return;
  }
  executeSceneHttp(scIt->second.sceneId.c_str(), tokIt->second.c_str());
}

// Запускает (или ставит в очередь с задержкой) все привязки для зоны+события
static void scheduleTriggers(const std::string &zoneName,
                             const std::string &event,
                             unsigned long      now)
{
  for (const auto &b : s_bindings)
  {
    if (b.zoneName != zoneName || b.event != event) continue;

    if (b.delayMs == 0)
    {
      fireScene(b.sceneKey);
    }
    else
    {
      // Убираем дубль, если такой pending уже есть (перезаход в зону)
      s_pendingFires.erase(
        std::remove_if(s_pendingFires.begin(), s_pendingFires.end(),
          [&](const PendingFire &pf) {
            return pf.zoneName == zoneName && pf.event == event &&
                   pf.sceneKey == b.sceneKey;
          }),
        s_pendingFires.end());

      s_pendingFires.push_back({zoneName, event, b.sceneKey, now + b.delayMs});
      Serial.printf("[SC] Scheduled %s.%s -> %s in %lums\n",
                    zoneName.c_str(), event.c_str(), b.sceneKey.c_str(), b.delayMs);
    }
  }
}

// ===================================================================
//  Токены
// ===================================================================

void addToken(const char *tokenId, const char *token)
{
  s_tokens[tokenId] = token;
  saveSceneConfig();
  Serial.printf("[SC] Token added: %s\n", tokenId);
}

void removeToken(const char *tokenId)
{
  s_tokens.erase(tokenId);
  saveSceneConfig();
  Serial.printf("[SC] Token removed: %s\n", tokenId);
}

// ===================================================================
//  Сцены
// ===================================================================

void addScene(const char *sceneKey, const char *sceneId, const char *tokenId)
{
  s_scenes[sceneKey] = {sceneId, tokenId};
  saveSceneConfig();
  Serial.printf("[SC] Scene added: %s -> %s (token: %s)\n", sceneKey, sceneId, tokenId);
}

void removeScene(const char *sceneKey)
{
  s_scenes.erase(sceneKey);
  saveSceneConfig();
  Serial.printf("[SC] Scene removed: %s\n", sceneKey);
}

// ===================================================================
//  Привязки
// ===================================================================

void bindZone(const char *zoneName, const char *ev, const char *sceneKey,
              unsigned long delayMs)
{
  std::string event = normalizeZoneEvent(ev);

  // Если привязка уже есть — обновляем delay
  for (auto &b : s_bindings)
  {
    if (b.zoneName == zoneName && b.event == event && b.sceneKey == sceneKey)
    {
      b.delayMs = delayMs;
      saveSceneConfig();
      Serial.printf("[SC] Bind updated: %s.%s -> %s (delay: %lums)\n",
                    zoneName, event.c_str(), sceneKey, delayMs);
      return;
    }
  }

  s_bindings.push_back({zoneName, event, sceneKey, delayMs});
  saveSceneConfig();
  Serial.printf("[SC] Bind: %s.%s -> %s (delay: %lums)\n",
                zoneName, event.c_str(), sceneKey, delayMs);
}

void unbindZone(const char *zoneName, const char *ev, const char *sceneKey)
{
  std::string event = normalizeZoneEvent(ev);

  s_bindings.erase(
    std::remove_if(s_bindings.begin(), s_bindings.end(),
      [&](const Binding &b) {
        return b.zoneName == zoneName && b.event == event && b.sceneKey == sceneKey;
      }),
    s_bindings.end());

  // Убираем и pending fires для этой привязки
  s_pendingFires.erase(
    std::remove_if(s_pendingFires.begin(), s_pendingFires.end(),
      [&](const PendingFire &pf) {
        return pf.zoneName == zoneName && pf.event == event && pf.sceneKey == sceneKey;
      }),
    s_pendingFires.end());

  saveSceneConfig();
  Serial.printf("[SC] Unbind: %s.%s -> %s\n", zoneName, event.c_str(), sceneKey);
}

// ===================================================================
//  Движок присутствия
// ===================================================================

void checkZonesPresence(const std::string &json, unsigned long now)
{
  // --- 1. Срабатывание отложенных событий ---
  auto it = s_pendingFires.begin();
  while (it != s_pendingFires.end())
  {
    if (now >= it->fireAt)
    {
      bool shouldFire = true;
      // Для "enter": отменяем если человек уже вышел до истечения delay
      if (it->event == "enter")
      {
        auto stateIt = s_zoneStates.find(it->zoneName);
        if (stateIt != s_zoneStates.end() && !stateIt->second.wasInside)
          shouldFire = false;
      }

      if (shouldFire)
      {
        Serial.printf("[SC] Fire delayed %s.%s -> %s\n",
                      it->zoneName.c_str(), it->event.c_str(), it->sceneKey.c_str());
        fireScene(it->sceneKey);
      }
      it = s_pendingFires.erase(it);
    }
    else
    {
      ++it;
    }
  }

  // --- 2. Детектирование присутствия по JSON ---
  size_t pizPos = json.find("personsInZones");

  // Собираем уникальные имена зон с привязками
  std::map<std::string, bool> zonesToCheck;
  for (const auto &b : s_bindings)
    zonesToCheck[b.zoneName] = true;

  for (auto &kv : zonesToCheck)
  {
    const std::string &zoneName = kv.first;

    // Ищем имя зоны ТОЛЬКО в части после "personsInZones"
    // Граничная проверка: следующий символ не буква/цифра/_
    bool inside = false;
    if (pizPos != std::string::npos)
    {
      size_t searchFrom = pizPos;
      while (true)
      {
        size_t namePos = json.find(zoneName, searchFrom);
        if (namePos == std::string::npos) break;

        size_t endPos  = namePos + zoneName.length();
        char   nextChar = (endPos < json.length()) ? json[endPos] : '\0';
        if (!isalnum((unsigned char)nextChar) && nextChar != '_')
        {
          inside = true;
          break;
        }
        searchFrom = namePos + 1;
      }
    }

    ZoneState &state = s_zoneStates[zoneName];
    if (inside) state.lastSeen = now;

    // ВХОД: был снаружи → стал внутри
    if (inside && !state.wasInside)
    {
      state.wasInside = true;
      // Отменяем ожидающие "leave" для этой зоны (повторный вход до срабатывания)
      s_pendingFires.erase(
        std::remove_if(s_pendingFires.begin(), s_pendingFires.end(),
          [&](const PendingFire &pf) {
            return pf.zoneName == zoneName && pf.event == "leave";
          }),
        s_pendingFires.end());
      Serial.printf("[SC] Enter: %s\n", zoneName.c_str());
      scheduleTriggers(zoneName, "enter", now);
    }
    // ВЫХОД: был внутри → цели нет дольше ZONE_LEAVE_TIMEOUT
    else if (state.wasInside && !inside && (now - state.lastSeen > ZONE_LEAVE_TIMEOUT))
    {
      state.wasInside = false;
      // Отменяем ожидающие "enter" для этой зоны (ушёл до истечения delay)
      s_pendingFires.erase(
        std::remove_if(s_pendingFires.begin(), s_pendingFires.end(),
          [&](const PendingFire &pf) {
            return pf.zoneName == zoneName && pf.event == "enter";
          }),
        s_pendingFires.end());
      Serial.printf("[SC] Leave: %s\n", zoneName.c_str());
      scheduleTriggers(zoneName, "leave", now);
    }
  }
}

// ===================================================================
//  Дамп конфига
// ===================================================================

std::string getConfigAsJson()
{
  DynamicJsonDocument doc(6144);

  JsonObject tokens = doc.createNestedObject("tokens");
  for (const auto &kv : s_tokens)
    tokens[kv.first.c_str()] = kv.second.c_str();

  JsonObject scenes = doc.createNestedObject("scenes");
  for (const auto &kv : s_scenes)
  {
    JsonObject s = scenes.createNestedObject(kv.first.c_str());
    s["sceneId"] = kv.second.sceneId.c_str();
    s["tokenId"] = kv.second.tokenId.c_str();
  }

  JsonArray bindings = doc.createNestedArray("bindings");
  for (const auto &b : s_bindings)
  {
    JsonObject obj = bindings.createNestedObject();
    obj["zoneName"] = b.zoneName.c_str();
    obj["event"]    = b.event.c_str();
    obj["sceneKey"] = b.sceneKey.c_str();
    obj["delayMs"]  = b.delayMs;
  }

  String out;
  serializeJson(doc, out);
  return std::string(out.c_str());
}

// ===================================================================
//  Persistence — NVS через Preferences
//  Компактный JSON, весь конфиг в одном ключе "config" (namespace "scene_cfg")
// ===================================================================

void saveSceneConfig()
{
  DynamicJsonDocument doc(6144);

  JsonObject tokens = doc.createNestedObject("t");
  for (const auto &kv : s_tokens)
    tokens[kv.first.c_str()] = kv.second.c_str();

  JsonObject scenes = doc.createNestedObject("s");
  for (const auto &kv : s_scenes)
  {
    JsonObject s = scenes.createNestedObject(kv.first.c_str());
    s["i"] = kv.second.sceneId.c_str();
    s["t"] = kv.second.tokenId.c_str();
  }

  JsonArray bindings = doc.createNestedArray("b");
  for (const auto &b : s_bindings)
  {
    JsonObject obj = bindings.createNestedObject();
    obj["z"] = b.zoneName.c_str();
    obj["e"] = b.event.c_str();
    obj["k"] = b.sceneKey.c_str();
    obj["d"] = b.delayMs;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);

  Preferences prefs;
  prefs.begin("scene_cfg", false);
  prefs.putString("config", jsonStr);
  prefs.end();

  Serial.printf("[SC] Config saved (%u bytes)\n", jsonStr.length());
}

void loadSceneConfig()
{
  Preferences prefs;
  prefs.begin("scene_cfg", true);
  String jsonStr = prefs.getString("config", "");
  prefs.end();

  if (jsonStr.isEmpty())
  {
    Serial.println("[SC] No stored config, starting empty");
    return;
  }

  DynamicJsonDocument doc(6144);
  DeserializationError err = deserializeJson(doc, jsonStr);
  if (err)
  {
    Serial.printf("[SC] Config parse error: %s\n", err.c_str());
    return;
  }

  s_tokens.clear();
  s_scenes.clear();
  s_bindings.clear();

  if (doc.containsKey("t"))
    for (JsonPair kv : doc["t"].as<JsonObject>())
      s_tokens[kv.key().c_str()] = kv.value().as<const char *>();

  if (doc.containsKey("s"))
    for (JsonPair kv : doc["s"].as<JsonObject>())
    {
      SceneEntry e;
      e.sceneId = kv.value()["i"].as<const char *>();
      e.tokenId = kv.value()["t"].as<const char *>();
      s_scenes[kv.key().c_str()] = e;
    }

  if (doc.containsKey("b"))
    for (JsonObject obj : doc["b"].as<JsonArray>())
      s_bindings.push_back({
        obj["z"].as<const char *>(),
        obj["e"].as<const char *>(),
        obj["k"].as<const char *>(),
        (unsigned long)(obj["d"] | 0)
      });

  Serial.printf("[SC] Config loaded: %d tokens, %d scenes, %d bindings\n",
                (int)s_tokens.size(), (int)s_scenes.size(), (int)s_bindings.size());
}
