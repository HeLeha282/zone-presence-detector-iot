#include "mqtt_handler.h"
#include "globals.h"
#include "config.h"
#include "camera.h"
#include <ArduinoJson.h>
#include "Ld2450.h"
#include <string>
#include "scene_config.h"

// ================= RECONNECT =================
void reconnect()
{
  Serial.print("MQTT connecting...");
  if (client.connect("ESP_Client_ID"))
  {
    Serial.println(" OK");
    std::string topic = "assistants/" + std::to_string(ASSISTANT_ID) + "/commands/#";
    client.subscribe(topic.c_str());
    Serial.printf("Subscribed to: %s\n", topic.c_str());
  }
  else
  {
    Serial.printf(" FAIL (rc=%d)\n", client.state());
  }
}

// ===================================================================
//  Вспомогательный макрос: формирует топик ответа из топика команды
// ===================================================================
static String makeResponseTopic(const char *cmdTopic)
{
  String t(cmdTopic);
  t.replace("commands", "response");
  return t;
}

// ===================================================================
//  CALLBACK: входящие команды
// ===================================================================
void callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++)
    message += (char)payload[i];

  Serial.println("\n=== MQTT Message ===");
  Serial.printf("Topic: %s\n", topic);
  Serial.printf("Payload: %s\n", message.c_str());

  // 1024 байт достаточно для всех входящих команд
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, message) != DeserializationError::Ok || !doc.containsKey("command"))
  {
    Serial.println("[MQTT] Bad JSON or missing 'command'");
    return;
  }

  String command   = doc["command"].as<String>();
  const char *rid  = doc["requestId"] | ""; // requestId — опциональное поле
  String respTopic = makeResponseTopic(topic);

  // ===================================================================
  //  Буфер для коротких ответов (достаточно для имён + UUID + requestId)
  // ===================================================================
  char resp[384];

  // ========== КОМАНДА: take_photo ==========
  if (command == "take_photo")
  {
    if (!doc.containsKey("requestId"))
    {
      Serial.println("[MQTT] 'requestId' required for take_photo");
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"take_photo\","
               "\"message\":\"requestId required\"}");
      client.publish(respTopic.c_str(), resp);
      return;
    }
    Serial.println("=== Triggered Photo Capture ===");
    sendPhotoSimple(String(rid));
    delay(2000);
  }

  // ========== КОМАНДА: add_zone ==========
  else if (command == "add_zone")
  {
    if (!doc.containsKey("zoneName") || !doc.containsKey("vertices") ||
        !doc["vertices"].is<JsonArray>())
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"add_zone\",\"requestId\":\"%s\","
               "\"message\":\"zoneName and vertices[] required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }

    String    zoneName      = doc["zoneName"].as<String>();
    JsonArray verticesArray = doc["vertices"].as<JsonArray>();

    if (verticesArray.size() < 3)
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"add_zone\",\"requestId\":\"%s\","
               "\"message\":\"at least 3 vertices required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }

    Zone newZone(zoneName.c_str());
    for (JsonObject v : verticesArray)
    {
      if (!v.containsKey("x") || !v.containsKey("y"))
      {
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"error\",\"command\":\"add_zone\",\"requestId\":\"%s\","
                 "\"message\":\"each vertex must have x and y\"}", rid);
        client.publish(respTopic.c_str(), resp);
        return;
      }
      newZone.addPoint(v["x"].as<int>(), v["y"].as<int>());
    }

    ld2450.addZone(newZone);

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"add_zone\",\"requestId\":\"%s\","
             "\"zoneName\":\"%s\",\"pointCount\":%d}",
             rid, zoneName.c_str(), newZone.getPointCount());
    client.publish(respTopic.c_str(), resp);
    Serial.printf("[MQTT] Zone '%s' added (%d pts)\n", zoneName.c_str(), newZone.getPointCount());
  }

  // ========== КОМАНДА: remove_zone ==========
  else if (command == "remove_zone")
  {
    if (!doc.containsKey("zoneName"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"remove_zone\",\"requestId\":\"%s\","
               "\"message\":\"zoneName required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }

    String zoneName = doc["zoneName"].as<String>();
    ld2450.removeZone(zoneName.c_str());

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"remove_zone\",\"requestId\":\"%s\","
             "\"zoneName\":\"%s\"}", rid, zoneName.c_str());
    client.publish(respTopic.c_str(), resp);
    Serial.printf("[MQTT] Zone '%s' removed\n", zoneName.c_str());
  }

  // ========== КОМАНДА: list_zones ==========
  else if (command == "list_zones")
  {
    DynamicJsonDocument responseDoc(4096);
    responseDoc["status"]    = "success";
    responseDoc["command"]   = "list_zones";
    responseDoc["requestId"] = rid;

    JsonArray zonesArray = responseDoc.createNestedArray("zones");

    for (int i = 0; i < ld2450.getZoneCount() && i < 10; i++)
    {
      Zone     &zone    = ld2450.getZone(i);
      JsonObject zoneObj = zonesArray.createNestedObject();
      zoneObj["name"]       = zone.getName();
      zoneObj["pointCount"] = zone.getPointCount();

      JsonArray            vArr     = zoneObj.createNestedArray("vertices");
      const std::vector<Point> &pts = zone.getVertices();
      for (const Point &p : pts)
      {
        JsonObject vo = vArr.createNestedObject();
        vo["x"] = p.x;
        vo["y"] = p.y;
      }
    }

    String responseMsg;
    serializeJson(responseDoc, responseMsg);

    if (responseMsg.length() < 1500)
      client.publish(respTopic.c_str(), responseMsg.c_str());
    else
      client.publish(respTopic.c_str(),
                     "{\"status\":\"error\",\"message\":\"Response too large\"}");

    Serial.println("[MQTT] Zone list sent");
  }

  // ========== КОМАНДА: clear_zones ==========
  else if (command == "clear_zones")
  {
    ld2450.clearAllZones();
    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"clear_zones\",\"requestId\":\"%s\"}", rid);
    client.publish(respTopic.c_str(), resp);
    Serial.println("[MQTT] All zones cleared");
  }

  // ========== ТОКЕНЫ ==========
  else if (command == "add_token")
  {
    if (!doc.containsKey("tokenId") || !doc.containsKey("token"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"add_token\",\"requestId\":\"%s\","
               "\"message\":\"tokenId and token required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }
    String tokenId = doc["tokenId"].as<String>();
    addToken(tokenId.c_str(), doc["token"].as<String>().c_str());

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"add_token\",\"requestId\":\"%s\","
             "\"tokenId\":\"%s\"}", rid, tokenId.c_str());
    client.publish(respTopic.c_str(), resp);
  }

  else if (command == "remove_token")
  {
    if (!doc.containsKey("tokenId"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"remove_token\",\"requestId\":\"%s\","
               "\"message\":\"tokenId required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }
    String tokenId = doc["tokenId"].as<String>();
    removeToken(tokenId.c_str());

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"remove_token\",\"requestId\":\"%s\","
             "\"tokenId\":\"%s\"}", rid, tokenId.c_str());
    client.publish(respTopic.c_str(), resp);
  }

  // ========== СЦЕНЫ ==========
  else if (command == "add_scene")
  {
    if (!doc.containsKey("sceneKey") || !doc.containsKey("sceneId") ||
        !doc.containsKey("tokenId"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"add_scene\",\"requestId\":\"%s\","
               "\"message\":\"sceneKey, sceneId and tokenId required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }
    String sceneKey = doc["sceneKey"].as<String>();
    addScene(sceneKey.c_str(),
             doc["sceneId"].as<String>().c_str(),
             doc["tokenId"].as<String>().c_str());

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"add_scene\",\"requestId\":\"%s\","
             "\"sceneKey\":\"%s\"}", rid, sceneKey.c_str());
    client.publish(respTopic.c_str(), resp);
  }

  else if (command == "remove_scene")
  {
    if (!doc.containsKey("sceneKey"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"remove_scene\",\"requestId\":\"%s\","
               "\"message\":\"sceneKey required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }
    String sceneKey = doc["sceneKey"].as<String>();
    removeScene(sceneKey.c_str());

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"remove_scene\",\"requestId\":\"%s\","
             "\"sceneKey\":\"%s\"}", rid, sceneKey.c_str());
    client.publish(respTopic.c_str(), resp);
  }

  // ========== ПРИВЯЗКИ ==========
  else if (command == "bind_zone")
  {
    if (!doc.containsKey("zoneName") || !doc.containsKey("event") ||
        !doc.containsKey("sceneKey"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"bind_zone\",\"requestId\":\"%s\","
               "\"message\":\"zoneName, event and sceneKey required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }

    String        zoneName  = doc["zoneName"].as<String>();
    String        rawEvent  = doc["event"].as<String>();
    String        sceneKey  = doc["sceneKey"].as<String>();
    unsigned long delayMs   = (unsigned long)(doc["delay"] | 0);

    // Нормализуем для отображения в ответе
    std::string normEvent = normalizeZoneEvent(rawEvent.c_str());

    bindZone(zoneName.c_str(), rawEvent.c_str(), sceneKey.c_str(), delayMs);

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"bind_zone\",\"requestId\":\"%s\","
             "\"zoneName\":\"%s\",\"event\":\"%s\",\"sceneKey\":\"%s\",\"delayMs\":%lu}",
             rid, zoneName.c_str(), normEvent.c_str(), sceneKey.c_str(), delayMs);
    client.publish(respTopic.c_str(), resp);
  }

  else if (command == "unbind_zone")
  {
    if (!doc.containsKey("zoneName") || !doc.containsKey("event") ||
        !doc.containsKey("sceneKey"))
    {
      snprintf(resp, sizeof(resp),
               "{\"status\":\"error\",\"command\":\"unbind_zone\",\"requestId\":\"%s\","
               "\"message\":\"zoneName, event and sceneKey required\"}", rid);
      client.publish(respTopic.c_str(), resp);
      return;
    }

    String    zoneName = doc["zoneName"].as<String>();
    String    rawEvent = doc["event"].as<String>();
    String    sceneKey = doc["sceneKey"].as<String>();
    std::string normEvent = normalizeZoneEvent(rawEvent.c_str());

    unbindZone(zoneName.c_str(), rawEvent.c_str(), sceneKey.c_str());

    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"command\":\"unbind_zone\",\"requestId\":\"%s\","
             "\"zoneName\":\"%s\",\"event\":\"%s\",\"sceneKey\":\"%s\"}",
             rid, zoneName.c_str(), normEvent.c_str(), sceneKey.c_str());
    client.publish(respTopic.c_str(), resp);
  }

  // ========== ДАМП КОНФИГА ==========
  else if (command == "get_config")
  {
    std::string cfg = getConfigAsJson();

    // Оборачиваем конфиг в общий конверт с status/command/requestId
    String full;
    full.reserve(cfg.length() + 80);
    full  = "{\"status\":\"success\",\"command\":\"get_config\",\"requestId\":\"";
    full += rid;
    full += "\",\"config\":";
    full += cfg.c_str();
    full += "}";

    client.publish(respTopic.c_str(), full.c_str());
    Serial.println("[MQTT] Config sent");
  }

  else
  {
    snprintf(resp, sizeof(resp),
             "{\"status\":\"error\",\"command\":\"%s\",\"requestId\":\"%s\","
             "\"message\":\"unknown command\"}", command.c_str(), rid);
    client.publish(respTopic.c_str(), resp);
    Serial.printf("[MQTT] Unknown command: %s\n", command.c_str());
  }
}
