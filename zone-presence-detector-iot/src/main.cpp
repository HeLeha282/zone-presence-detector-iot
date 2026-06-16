#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <string>

#include "config.h"
#include "globals.h"
#include "camera.h"
#include "scene_config.h"
#include "mqtt_handler.h"
#include "Ld2450.h"

// ===================================================================
//  ОПРЕДЕЛЕНИЕ ГЛОБАЛЬНЫХ ОБЪЕКТОВ (объявлены в globals.h)
//  Порядок важен: ld2450 принимает client в конструкторе
// ===================================================================
WiFiClient espClient;
PubSubClient client(espClient);
Ld2450 ld2450(client);

// ================= SETUP =================
void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== SYSTEM START ===");

  // Подключение к WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40)
  {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("\nWiFi connection failed!");
    ESP.restart();
  }

  Serial.println("\nWiFi connected!");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());

  // Загрузка конфига сцен из NVS
  loadSceneConfig();

  // Инициализация LD2450
  ld2450.begin();

  // Инициализация камеры
  initCamera();
  delay(1000);

  // Настройка WiFi для стабильности
  WiFi.setSleep(false);

  // MQTT настройка
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  if (!client.setBufferSize(3064))
  {
    Serial.println("Ошибка: не удалось выделить память для буфера MQTT!");
  }

  // Зона по умолчанию
  Zone livingRoom("LivingRoom");
  livingRoom.addPoint(0, 0);
  livingRoom.addPoint(1000, 0);
  livingRoom.addPoint(1000, 1000);
  livingRoom.addPoint(0, 1000);
  ld2450.addZone(livingRoom);

  // Подключение к MQTT
  if (client.connect("ESP_Client_ID"))
  {
    std::string topic = "assistants/" + std::to_string(ASSISTANT_ID) + "/commands/#";
    client.subscribe(topic.c_str());
    Serial.println("МКТТ ПОДКЛ");
  }
  else
  {
    Serial.printf("МКТТ НЕ ПОДКЛ (rc=%d)\n", client.state());
  }

  Serial.println("=== SYSTEM READY ===\n");
}

// ================= LOOP =================
unsigned long lastMsg = 0;
unsigned long lastStatsTime = 0;
const long statsInterval = 5000;
unsigned long lastMqttReconnect = 0;

void loop()
{
  // MQTT reconnect
  if (!client.connected())
  {
    unsigned long now = millis();
    if (now - lastMqttReconnect > 5000)
    {
      lastMqttReconnect = now;
      reconnect();
    }
  }
  else
  {
    client.loop();
  }

  unsigned long now = millis();

  // Статистика
  if (now - lastStatsTime >= statsInterval)
  {
    lastStatsTime = now;

    Serial.println("\n=========================================");
    Serial.printf("Free RAM: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("Min Free RAM: %u bytes\n", ESP.getMinFreeHeap());
    Serial.printf("Uptime: %lu sec\n", now / 1000);
  }

  // Отправка данных с радара + проверка зоны
  if (now - lastMsg > 100)
  {
    lastMsg = now;

    std::string s = ld2450.getDataFromSensorInJSON();

    if (!s.empty())
    {
      std::string topic = "assistants/" + std::to_string(ASSISTANT_ID) + "/ld2450";
      client.publish(topic.c_str(), s.c_str());
      checkZonesPresence(s, now);
    }
  }

  delay(10);
}
