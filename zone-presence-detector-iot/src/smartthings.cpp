#include "smartthings.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ================= ВЫПОЛНИТЬ СЦЕНУ SMARTTHINGS (HTTPS POST) =================
bool executeSceneHttp(const char *sceneId, const char *token)
{
  WiFiClientSecure secure;
  secure.setInsecure(); // сертификат ST не проверяем

  HTTPClient http;
  String url = "https://api.smartthings.com/v1/scenes/" + String(sceneId) + "/execute";

  if (!http.begin(secure, url))
  {
    Serial.printf("[ST] begin failed for scene %s\n", sceneId);
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(token));
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(""); // тело пустое
  bool ok  = (code == 200);

  if (code > 0)
    Serial.printf("[ST] scene %s -> HTTP %d\n", sceneId, code);
  else
    Serial.printf("[ST] scene %s -> error: %s\n", sceneId, http.errorToString(code).c_str());

  http.end();
  return ok;
}
