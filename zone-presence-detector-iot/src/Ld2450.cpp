#include <Arduino.h>
#include "Ld2450.h"
#include <sstream>
#include <stdio.h>
#include <time.h>

// ==========================
// CONSTRUCTOR
// ==========================
Ld2450::Ld2450(PubSubClient client)
{
   this->client = client;
}

// ==========================
// INIT UART
// ==========================
void Ld2450::begin()
{
   Serial1.begin(115200, SERIAL_8N1, 1, 2); // RX=1 TX=2
   // Serial.println("LD2450 UART started");
}

// ==========================
// READ + FRAME PARSER
// ==========================
std::string Ld2450::getDataFromSensorInJSON()
{
   uint8_t incomingByte;
   // Serial.println("ИТЕРАЦИЯ в FIND 1");
   while (Serial1.available() > 0)
   {
      incomingByte = Serial1.read();

      // Serial.print("ИТЕРАЦИЯ в FIND , пришло = ");
      // Serial.println(incomingByte);
      // START OF FRAME
      if (bufferIndex == 0)
      {
         if (incomingByte == 0xAA)
         {
            // Serial.println("НАШЕЛ АА");

            buffer[bufferIndex++] = incomingByte;
         }
         continue;
      }

      // SECOND BYTE CHECK
      if (bufferIndex == 1)
      {
         if (incomingByte == 0xFF)
         {
            buffer[bufferIndex++] = incomingByte;
         }
         else
         {
            bufferIndex = 0;
         }
         continue;
      }

      // FILL FRAME
      if (bufferIndex > 1 && bufferIndex < FRAME_LENGTH)
      {
         buffer[bufferIndex++] = incomingByte;

         if (bufferIndex == FRAME_LENGTH)
         {
            bufferIndex = 0;

            // HEADER + FOOTER CHECK (как у тебя в ESP-IDF)
            if (buffer[0] == 0xAA &&
                buffer[1] == 0xFF &&
                buffer[2] == 0x03 &&
                buffer[3] == 0x00 &&
                buffer[28] == 0x55 &&
                buffer[29] == 0xCC)
            {
               return parseData();
            }
         }
      }
      else
      {
         bufferIndex = 0;
      }
   }

   return "";
}

// ==========================
// INT PARSER (без изменений)
// ==========================
int16_t Ld2450::parseCustomInt16(uint8_t low, uint8_t high)
{
   uint16_t raw = (high << 8) | low;
   int16_t value = raw & 0x7FFF;
   bool isPositive = (raw >> 15) & 0x01;

   return isPositive ? value : -value;
}

// ==========================
// FRAME PARSER (логика сохранена)
// ==========================
std::string Ld2450::parseData()
{
   std::string targets_json;
   targets_json += "\"targets\":[";

   std::string zones_info = printAllZonesInJSON();

   int targetsFound = 0;
   std::string personsInZones; // Для хранения информации о людях в зонах

   for (int i = 0; i < 3; i++)
   {
      int offset = 4 + (i * 8);

      uint8_t xLow = buffer[offset];
      uint8_t xHigh = buffer[offset + 1];
      uint8_t yLow = buffer[offset + 2];
      uint8_t yHigh = buffer[offset + 3];
      uint8_t sLow = buffer[offset + 4];
      uint8_t sHigh = buffer[offset + 5];

      if (xLow == 0 && xHigh == 0 && yLow == 0 && yHigh == 0)
         continue;

      int16_t x = parseCustomInt16(xLow, xHigh);
      int16_t y = parseCustomInt16(yLow, yHigh);
      int16_t speed = parseCustomInt16(sLow, sHigh);

      // Check if point is in any zone
      std::string zonesForPoint = getZonesForPoint(x, y);
      bool inZone = !zonesForPoint.empty();

      if (inZone)
      {
         Serial.print("Person detected in zone(s): ");
         Serial.print(zonesForPoint.c_str());
         Serial.print(" at position (");
         Serial.print(x);
         Serial.print(", ");
         Serial.print(y);
         Serial.println(")");

         if (!personsInZones.empty())
            personsInZones += ",";
         char zoneBuf[128];
         snprintf(zoneBuf, sizeof(zoneBuf),
                  "{\"targetId\":%d,\"zones\":\"%s\",\"x\":%d,\"y\":%d,\"speed\":%d}",
                  i + 1, zonesForPoint.c_str(), x, y, speed);
         personsInZones += zoneBuf;
      }

      if (targetsFound > 0)
         targets_json += ",";

      char buf[128];
      snprintf(buf, sizeof(buf),
               "{\"id\":%d,\"x\":%d,\"y\":%d,\"speed\":%d,\"inZone\":%s}",
               i + 1, x, y, speed, inZone ? "true" : "false");

      targets_json += buf;
      targetsFound++;
   }

   targets_json += "]";

   // Формируем полный JSON ответ
   std::string full_json = "{";
   full_json += targets_json;
   full_json += ",";
   full_json += zones_info;

   // Добавляем информацию о людях в зонах
   if (!personsInZones.empty())
   {
      full_json += ",\"personsInZones\":[";
      full_json += personsInZones;
      full_json += "]";
   }

   full_json += "}";

   // Serial.print("ВОзоварщаю ");
   // Serial.println(full_json.c_str());
   return full_json;
}

// ==========================
// TASK LOOP
// ==========================
void Ld2450::loop_uart_read_data(void *pvParameters)
{
   Ld2450 *self = static_cast<Ld2450 *>(pvParameters);

   // Serial.println("LD2450 task started");

   while (1)
   {
      std::string json = self->getDataFromSensorInJSON();

      if (!json.empty())
      {
         self->client.publish("assistants/1/ld2450", json.c_str());
      }

      vTaskDelay(10 / portTICK_PERIOD_MS);
   }
}

// ==========================
// START TASK
// ==========================
void Ld2450::startTask()
{
   xTaskCreate(
       loop_uart_read_data,
       "ld2450_task",
       8192,
       this,
       2,
       NULL);
}

// ==========================
// ZONE IMPLEMENTATION
// ==========================

// Zone class implementation
Zone::Zone(const std::string &name) : zoneName(name) {}

void Zone::addPoint(int16_t x, int16_t y)
{
   vertices.push_back(Point(x, y));
}

void Zone::addPoint(const Point &point)
{
   vertices.push_back(point);
}

void Zone::clearPoints()
{
   vertices.clear();
}

bool Zone::containsPoint(int16_t x, int16_t y) const
{
   return containsPoint(Point(x, y));
}

bool Zone::containsPoint(const Point &point) const
{
   if (vertices.size() < 3)
      return false; // Need at least 3 points for a polygon

   bool inside = false;
   size_t n = vertices.size();

   for (size_t i = 0, j = n - 1; i < n; j = i++)
   {
      const Point &vi = vertices[i];
      const Point &vj = vertices[j];

      // Check if the point is on the edge (optional, can be included or excluded)
      // Ray casting algorithm
      if (((vi.y > point.y) != (vj.y > point.y)) &&
          (point.x < (vj.x - vi.x) * (point.y - vi.y) / (vj.y - vi.y) + vi.x))
      {
         inside = !inside;
      }
   }

   return inside;
}

const std::string &Zone::getName() const
{
   return zoneName;
}

size_t Zone::getPointCount() const
{
   return vertices.size();
}

const std::vector<Point> &Zone::getVertices() const
{
   return vertices;
}

void Zone::printZone() const
{
   Serial.print("Zone '");
   Serial.print(zoneName.c_str());
   Serial.print("' has ");
   Serial.print(vertices.size());
   Serial.println(" vertices:");

   for (size_t i = 0; i < vertices.size(); i++)
   {
      Serial.print("  Point ");
      Serial.print(i);
      Serial.print(": (");
      Serial.print(vertices[i].x);
      Serial.print(", ");
      Serial.print(vertices[i].y);
      Serial.println(")");
   }
}

// Ld2450 Zone management methods
void Ld2450::addZone(const Zone &zone)
{
   zones.push_back(zone);
   Serial.print("Zone '");
   Serial.print(zone.getName().c_str());
   Serial.println("' added successfully");
   zone.printZone();
}

void Ld2450::addZone(const std::string &zoneName, const std::vector<Point> &points)
{
   Zone newZone(zoneName);
   for (const auto &point : points)
   {
      newZone.addPoint(point);
   }
   addZone(newZone);
}

void Ld2450::addZone(const std::string &zoneName)
{
   Zone newZone(zoneName);
   zones.push_back(newZone);
   Serial.print("Empty zone '");
   Serial.print(zoneName.c_str());
   Serial.println("' created. Use getZone() to add points.");
}

Zone &Ld2450::getZone(int index)
{
   if (index >= 0 && index < (int)zones.size())
   {
      return zones[index];
   }
   static Zone emptyZone;
   Serial.println("Error: Zone index out of range");
   return emptyZone;
}

Zone &Ld2450::getZone(const std::string &zoneName)
{
   for (auto &zone : zones)
   {
      if (zone.getName() == zoneName)
      {
         return zone;
      }
   }
   static Zone emptyZone;
   Serial.print("Error: Zone '");
   Serial.print(zoneName.c_str());
   Serial.println("' not found");
   return emptyZone;
}

void Ld2450::removeZone(int index)
{
   if (index >= 0 && index < (int)zones.size())
   {
      Serial.print("Removing zone '");
      Serial.print(zones[index].getName().c_str());
      Serial.println("'");
      zones.erase(zones.begin() + index);
   }
   else
   {
      Serial.println("Error: Cannot remove zone - index out of range");
   }
}

void Ld2450::removeZone(const std::string &zoneName)
{
   for (auto it = zones.begin(); it != zones.end(); ++it)
   {
      if (it->getName() == zoneName)
      {
         Serial.print("Removing zone '");
         Serial.print(zoneName.c_str());
         Serial.println("'");
         zones.erase(it);
         return;
      }
   }
   Serial.print("Error: Zone '");
   Serial.print(zoneName.c_str());
   Serial.println("' not found for removal");
}

void Ld2450::clearAllZones()
{
   zones.clear();
   Serial.println("All zones cleared");
}

int Ld2450::getZoneCount() const
{
   return zones.size();
}

void Ld2450::printAllZones() const
{
   if (zones.empty())
   {
      Serial.println("No zones defined");
      return;
   }

   Serial.println("=== All Zones ===");
   for (size_t i = 0; i < zones.size(); i++)
   {
      Serial.print(i);
      Serial.print(": ");
      zones[i].printZone();
   }
}

bool Ld2450::pointInPolygon(int16_t x, int16_t y, const std::vector<Point> &polygon) const
{
   if (polygon.size() < 3)
      return false;

   bool inside = false;
   size_t n = polygon.size();

   for (size_t i = 0, j = n - 1; i < n; j = i++)
   {
      const Point &vi = polygon[i];
      const Point &vj = polygon[j];

      if (((vi.y > y) != (vj.y > y)) &&
          (x < (vj.x - vi.x) * (y - vi.y) / (vj.y - vi.y) + vi.x))
      {
         inside = !inside;
      }
   }

   return inside;
}

bool Ld2450::isPointInAnyZone(int16_t x, int16_t y) const
{
   for (const auto &zone : zones)
   {
      if (zone.containsPoint(x, y))
      {
         return true;
      }
   }
   return false;
}

std::string Ld2450::getZonesForPoint(int16_t x, int16_t y) const
{
   std::string result;
   bool first = true;

   for (const auto &zone : zones)
   {
      if (zone.containsPoint(x, y))
      {
         if (!first)
            result += ",";
         result += zone.getName();
         first = false;
      }
   }

   return result;
}

std::string Ld2450::printAllZonesInJSON() const
{
   if (zones.empty())
   {
      return "\"zones\":[]";
   }

   std::string result = "\"zones\":[";

   for (size_t i = 0; i < zones.size(); i++)
   {
      if (i > 0)
         result += ",";

      const Zone &zone = zones[i];

      result += "{";
      result += "\"name\":\"" + zone.getName() + "\",";

      // Добавляем вершины многоугольника
      result += "\"vertices\":[";

      const std::vector<Point> &vertices = zone.getVertices();
      for (size_t j = 0; j < vertices.size(); j++)
      {
         if (j > 0)
            result += ",";

         char vertexBuf[64];
         snprintf(vertexBuf, sizeof(vertexBuf),
                  "{\"x\":%d,\"y\":%d}",
                  vertices[j].x, vertices[j].y);
         result += vertexBuf;
      }

      result += "],";

      // Добавляем информацию о количестве точек
      char infoBuf[32];
      snprintf(infoBuf, sizeof(infoBuf),
               "\"pointCount\":%d", (int)vertices.size());
      result += infoBuf;

      result += "}";
   }

   result += "]";
   return result;
}