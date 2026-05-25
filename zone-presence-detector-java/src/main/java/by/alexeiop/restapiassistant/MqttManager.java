package by.alexeiop.restapiassistant;

import org.eclipse.paho.client.mqttv3.*;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.annotation.Bean;
import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;
import org.springframework.web.context.request.async.DeferredResult;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;


@Component
public class MqttManager {
  // Карта для связи: CorrelationId -> Ожидающий ответ
  public final Map<String, DeferredResult<String>> responseMap = new ConcurrentHashMap<>();

  // Готовый инструмент для парсинга JSON
  private final ObjectMapper objectMapper = new ObjectMapper();

  private static final String brokerUrl = "tcp://104.252.89.123"; // Адрес вашего Mosquitto
  private static final String clientId = "JavaSampleClient";
  MqttClient client;

  MqttManager(){}

  @EventListener(ApplicationReadyEvent.class)
  public void init(){
    try {
      client = new MqttClient(brokerUrl, clientId);
      // Настройка параметров подключения
      MqttConnectOptions options = new MqttConnectOptions();
      options.setCleanSession(true);
//      options.setUserName("your_user"); // Опционально
//      options.setPassword("your_password".toCharArray()); // Опционально

//      message.setRetained(true);

      System.out.println("Подключение к брокеру...");
      client.setCallback(new MqttCallback() {

        @Override
        public void connectionLost(Throwable cause) { //Called when the client lost the connection to the broker
        }

        @Override
        public void messageArrived(String topic, MqttMessage message) throws Exception {
          String payload = new String(message.getPayload());
          System.out.println("MQTT получено: " + payload);

          try {
            // Парсим JSON один раз
            JsonNode jsonResponse = objectMapper.readTree(payload);

            // Извлекаем данные по именам полей
            String requestId = jsonResponse.get("requestId").asText();
            String url = jsonResponse.get("url").asText();

            System.out.println("ВОт такой url подсе парсинга:" + url);

            // Ищем зависший HTTP запрос в нашей карте
            DeferredResult<String> output = responseMap.remove(requestId);

            if (output != null) {
//              output.setResult(url); // Клиент по HTTP мгновенно получает эту ссылку
                // Формируем JSON-строку вручную
                String jsonOutput = String.format("{\"url\":\"%s\", \"status\":\"success\"}", url);

                // Отправляем готовую строку. Клиент получит её как тело ответа.
                output.setResult(jsonOutput);
            } else {
              System.out.println("Запрос с ID " + requestId + " не найден (возможно, вышел таймаут)");
            }
          } catch (Exception e) {
            System.err.println("Ошибка парсинга сообщения: " + e.getMessage());
          }
        }

        @Override
        public void deliveryComplete(IMqttDeliveryToken token) {//Called when a outgoing publish is complete
        }
      });

      client.connect(options);
      client.subscribe("hello", 2);

//      client.connect(options);

      System.out.println("Клиент подключен: " + client.isConnected());

//      int i = 0;
//      while (i < 10) {
//        // Публикация сообщения
//        String content = "Hello from Java!" + i;
//        MqttMessage message = new MqttMessage(content.getBytes());
//        message.setQos(2);
//        client.publish("test/topic", message);
//
//        System.out.println("Сообщение отправлено");
//        sleep(1000);
//        i++;
//      }
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  public void sendMessage(String topic, String content){
    MqttMessage message = new MqttMessage(content.getBytes());
    message.setQos(2);
    try {
      client.publish(topic, message);
    } catch (MqttException e) {
      throw new RuntimeException(e);
    }
  }

  private String parseId(String payload) {
    try {
      // Превращаем строку в дерево JSON
      JsonNode jsonNode = objectMapper.readTree(payload);
      // Достаем значение поля "requestId"
      return jsonNode.get("requestId").asText();
    } catch (Exception e) {
      System.err.println("Не удалось найти requestId в JSON: " + payload);
      return null;
    }
  }

  private String parseUrl(String payload) {
    try {
      JsonNode jsonNode = objectMapper.readTree(payload);
      // Достаем значение поля "url" (или "link" — как назовете на устройстве)
      return jsonNode.get("url").asText();
    } catch (Exception e) {
      return "ошибка-ссылки";
    }
  }

}
