package by.alexeiop.restapiassistant;


import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.context.request.async.DeferredResult;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

@RestController
@RequestMapping("api/management_assistant")
public class ManagementAssistantController {

  private static final Logger logger = LoggerFactory.getLogger(ManagementAssistantController.class);
  ManagementAssistantService managementAssistantService;
  MqttManager mqttManager;

  @Autowired
  public ManagementAssistantController(ManagementAssistantService managementAssistantService, MqttManager mqttManager) {
    this.managementAssistantService = managementAssistantService;
    this.mqttManager = mqttManager;
  }

  @GetMapping(value = "/takePhoto/{id}", produces = "application/json")
// 1. Метод должен возвращать DeferredResult напрямую
  public DeferredResult<String> takePhoto(@PathVariable("id") long id) {
    String requestId = UUID.randomUUID().toString();

    // Создаем DeferredResult (таймаут 10 секунд)
    DeferredResult<String> output = new DeferredResult<>(100000L);

    // Обработка таймаута
    output.onTimeout(() -> {
      mqttManager.responseMap.remove(requestId);
      output.setErrorResult("Timeout: устройство не отвечает");
    });

    // 2. Кладем в карту менеджера, чтобы он мог найти этот запрос позже
    mqttManager.responseMap.put(requestId, output);

    // 3. Вызываем сервис, чтобы он отправил сообщение в MQTT
    // Важно: передаем requestId, чтобы ассистент знал, с каким ID вернуть ответ
    try {
      managementAssistantService.takePhoto(id, requestId);
    } catch (Exception e) {
      mqttManager.responseMap.remove(requestId);
      output.setErrorResult("Ошибка при отправке команды: " + e.getMessage());
    }

    // Метод завершается, но клиент ждет ответа, пока не сработает setResult в MqttManager
    return output;
  }
//  // produces - производит, consumes - потребляет
//  @GetMapping(value = "/takePhoto/{id}", produces = "application/json")
//  public ResponseEntity<String> takePhoto(@PathVariable("id") long id) {
//    String requestId = UUID.randomUUID().toString();
//
//    // Создаем DeferredResult с таймаутом (например, 10 секунд)
//    DeferredResult<String> output = new DeferredResult<>(10000L);
//
//    output.onTimeout(() -> {
//      responseMap.remove(requestId);
//      output.setErrorResult("Timeout: device not responding");
//    });
//
//    responseMap.put(requestId, output);
//
//    return output;
//
//    logger.info("execute takePhoto");
//    try {
//      String result = managementAssistantService.takePhoto(id);
//      return ResponseEntity.ok(result);
//    } catch (Exception e) {
//      return ResponseEntity.status(HttpStatus.BAD_REQUEST).body("takePhoto неудача");
//    }
//  }

  private final String UPLOAD_DIR = "photos/";


  @PostMapping("/upload-photo/{requestId}")
  public ResponseEntity<String> handlePhotoUpload(@RequestBody byte[] photoBytes, @PathVariable String requestId) {
    try {
      System.out.println("НАЧИНАЕМ ОБРАБОТКУ");
      // 1. Создаем папку, если её нет
      Path uploadPath = Paths.get(UPLOAD_DIR);
      if (!Files.exists(uploadPath)) {
        Files.createDirectories(uploadPath);
      }

      // 2. Генерируем уникальное имя файла (например, photo_20231027_153045.jpg)
      String timestamp = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss"));
      String fileName = "photo_" + timestamp + ".jpg";
      Path filePath = uploadPath.resolve(fileName);

      // 3. Записываем байты в файл
      Files.write(filePath, photoBytes);

      System.out.println("Фотка сохранена: " + filePath.toAbsolutePath());
      String url = "http://104.253.25.96:8080/" + fileName;

      DeferredResult<String> output = mqttManager.responseMap.remove(requestId);
      if (output != null) {
//              output.setResult(url); // Клиент по HTTP мгновенно получает эту ссылку
          // Формируем JSON-строку вручную

          String jsonOutput = String.format("{\"url\":\"%s\", \"status\":\"success\"}", url);

          // Отправляем готовую строку. Клиент получит её как тело ответа.
          output.setResult(jsonOutput);
      }
      else {
        System.out.println("Запрос с ID " + requestId + " не найден (возможно, вышел таймаут)");
      }
      return ResponseEntity.ok(url);

    } catch (IOException e) {
      e.printStackTrace();
      return ResponseEntity.status(500).body("Ошибка при сохранении файла");
    }
  }


}
