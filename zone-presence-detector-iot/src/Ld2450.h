#pragma once

#include <Arduino.h>
#include <string>
#include <PubSubClient.h>
#include <time.h>
#include <vector>

struct Point
{
   int16_t x;
   int16_t y;

   Point(int16_t x = 0, int16_t y = 0) : x(x), y(y) {}
};

class Zone
{
private:
   std::vector<Point> vertices;
   std::string zoneName;

public:
   Zone(const std::string &name = "");

   void addPoint(int16_t x, int16_t y);
   void addPoint(const Point &point);
   void clearPoints();

   bool containsPoint(int16_t x, int16_t y) const;
   bool containsPoint(const Point &point) const;

   const std::string &getName() const;
   size_t getPointCount() const;
   const std::vector<Point> &getVertices() const;

   void printZone() const;
};

class Ld2450
{
public:
   static const int FRAME_LENGTH = 30;

   // constructor
   Ld2450(PubSubClient client);

   // init UART
   void begin();

   // start FreeRTOS task
   void startTask();
   static void loop_uart_read_data(void *pvParameters);

   // main logic
   std::string getDataFromSensorInJSON();
   std::string parseData();
   int16_t parseCustomInt16(uint8_t low, uint8_t high);

   // Zone management API
   void addZone(const Zone &zone);
   void addZone(const std::string &zoneName, const std::vector<Point> &points);
   void addZone(const std::string &zoneName);
   Zone &getZone(int index);
   Zone &getZone(const std::string &zoneName);
   void removeZone(int index);
   void removeZone(const std::string &zoneName);
   void clearAllZones();
   int getZoneCount() const;
   void printAllZones() const;
   std::string printAllZonesInJSON() const;

   // Check if point is in any zone
   bool isPointInAnyZone(int16_t x, int16_t y) const;
   std::string getZonesForPoint(int16_t x, int16_t y) const;

private:
   PubSubClient client;
   std::vector<Zone> zones;

   uint8_t buffer[FRAME_LENGTH];
   uint8_t bufferIndex = 0;

   // Helper function for point-in-polygon test
   bool pointInPolygon(int16_t x, int16_t y, const std::vector<Point> &polygon) const;
};