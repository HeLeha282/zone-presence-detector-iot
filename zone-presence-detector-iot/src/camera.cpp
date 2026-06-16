#include "camera.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Состояние камеры
bool cameraIsHighMode = false;
bool cameraInitialized = false;

// ================= CAMERA INIT =================
void initCamera()
{
  Serial.println("=== Initializing Camera ===");

  // Начинаем с LOW-конфига (экономия)
  camera_config_t config = createCameraConfig(
      LOW_XCLK_HZ, LOW_FRAMESIZE, LOW_QUALITY, LOW_FB_COUNT, LOW_GRAB_MODE);

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    return;
  }

  applySensorSettingsFull(false);
  cameraIsHighMode = false;
  cameraInitialized = true;

  Serial.println("✅ Camera initialized in LOW mode");
}

// ================= SEND PHOTO TO SERVER (простой бинарный POST) =================
bool sendPhotoToServer()
{
  Serial.println("\n=== Capturing Photo ===");

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed!");
    return false;
  }

  Serial.printf("Photo captured: %u bytes\n", fb->len);
  Serial.println("Sending to server...");

  HTTPClient http;
  http.begin(PHOTO_SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");

  int httpResponseCode = http.POST(fb->buf, fb->len);

  String response = "";
  if (httpResponseCode > 0)
  {
    response = http.getString();
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    Serial.printf("Response: %s\n", response.c_str());
  }
  else
  {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  esp_camera_fb_return(fb);
  http.end();

  return (httpResponseCode == 200 || httpResponseCode == 201);
}

// ================= SEND PHOTO (SIMPLE - РАБОЧИЙ ВАРИАНТ) =================
bool sendPhotoSimple(String message)
{
  Serial.println("\n=== Capturing Photo ===");

  // 🔼 1. ПЕРЕД съёмкой: включаем HIGH режим
  setCameraHighMode();
  delay(300); // стабилизация после смены разрешения

  // 2. Получаем кадр
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("❌ Camera capture failed!");
    setCameraLowMode(); // возвращаем режим даже при ошибке
    return false;
  }

  Serial.printf("📸 Photo captured: %u bytes\n", fb->len);
  Serial.println("📤 Sending to server...");

  WiFiClient wifiClient;
  HTTPClient http;

  String fullUrl = String(PHOTO_SERVER_URL) + "/" + message;

  if (!http.begin(wifiClient, fullUrl))
  {
    Serial.println("❌ HTTP begin failed");
    esp_camera_fb_return(fb);
    return false;
  }

  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Content-Length", String(fb->len));

  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.printf("✅ HTTP Response: %d\n", httpResponseCode);
    if (!response.isEmpty())
    {
      Serial.printf("📝 Body: %s\n", response.c_str());
    }
  }
  else
  {
    Serial.printf("❌ HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  esp_camera_fb_return(fb);
  http.end();

  // 🔽 ПОСЛЕ отправки: возвращаем LOW режим
  setCameraLowMode();

  return (httpResponseCode == HTTP_CODE_OK || httpResponseCode == HTTP_CODE_CREATED);
}

// ================= CAMERA MODE SWITCHING (on-the-fly, не используется) =================
bool applySensorSettings(int framesize, int quality, bool highMode)
{
  sensor_t *s = esp_camera_sensor_get();
  if (!s)
  {
    Serial.println("❌ Failed to get camera sensor!");
    return false;
  }

  if (s->set_framesize(s, (framesize_t)framesize) != 0)
  {
    Serial.println("⚠️ Failed to set framesize");
    return false;
  }

  s->set_quality(s, quality);

  if (highMode)
  {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_sharpness(s, 1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_ae_level(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 30);
    s->set_gainceiling(s, GAINCEILING_4X);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
    Serial.println("🔼 Camera: HIGH mode applied");
  }
  else
  {
    s->set_brightness(s, 0);
    s->set_whitebal(s, 0);
    s->set_exposure_ctrl(s, 0);
    s->set_gain_ctrl(s, 0);
    Serial.println("🔽 Camera: LOW mode applied");
  }

  return true;
}

// ================= CAMERA CONFIG =================
camera_config_t createCameraConfig(int xclk_hz, int framesize, int quality,
                                   int fb_count, int grab_mode)
{
  camera_config_t config = {0};

  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;

  config.xclk_freq_hz = xclk_hz;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = (framesize_t)framesize;
  config.jpeg_quality = quality;
  config.fb_count     = fb_count;
  config.grab_mode    = (camera_grab_mode_t)grab_mode;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.ledc_timer   = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;

  return config;
}

// ================= APPLY SENSOR SETTINGS (общая) =================
void applySensorSettingsFull(bool highMode)
{
  sensor_t *s = esp_camera_sensor_get();
  if (!s)
    return;

  if (highMode)
  {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_sharpness(s, 1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_ae_level(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 30);
    s->set_gainceiling(s, GAINCEILING_4X);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
  }
  else
  {
    s->set_brightness(s, 0);
    s->set_whitebal(s, 0);
    s->set_exposure_ctrl(s, 0);
    s->set_gain_ctrl(s, 0);
  }
}

// ================= HIGH режим (полный реинит) =================
void setCameraHighMode()
{
  if (!cameraIsHighMode)
  {
    Serial.println("🔄 Switching to HIGH camera mode (full reinit)...");

    esp_camera_deinit();
    delay(300);

    camera_config_t config = createCameraConfig(
        HIGH_XCLK_HZ, HIGH_FRAMESIZE, HIGH_QUALITY, HIGH_FB_COUNT, HIGH_GRAB_MODE);

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
      Serial.printf("❌ Camera HIGH init failed: 0x%x\n", err);
      return;
    }

    applySensorSettingsFull(true);
    cameraIsHighMode = true;
    cameraInitialized = true;

    // Очистить очередь кадров
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb)
      esp_camera_fb_return(fb);

    Serial.println("✅ Camera in HIGH mode");
    delay(200);
  }
}

// ================= LOW режим (полный реинит) =================
void setCameraLowMode()
{
  if (cameraIsHighMode)
  {
    Serial.println("🔄 Switching to LOW camera mode (full reinit)...");

    esp_camera_deinit();
    delay(300);

    camera_config_t config = createCameraConfig(
        LOW_XCLK_HZ, LOW_FRAMESIZE, LOW_QUALITY, LOW_FB_COUNT, LOW_GRAB_MODE);

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
      Serial.printf("❌ Camera LOW init failed: 0x%x\n", err);
      return;
    }

    applySensorSettingsFull(false);
    cameraIsHighMode = false;
    cameraInitialized = true;

    Serial.println("✅ Camera in LOW mode");
    delay(100);
  }
}
