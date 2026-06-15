package by.alexeiop.restapiassistant;

import org.eclipse.paho.client.mqttv3.*;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;
import org.springframework.web.context.request.async.DeferredResult;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class MqttManager {

  public final Map<String, DeferredResult<String>> responseMap = new ConcurrentHashMap<>();
  private final ObjectMapper objectMapper = new ObjectMapper();

  private static final String BROKER_URL = "tcp://45.39.190.176";
  private static final String CLIENT_ID  = "JavaSampleClient";

  // Топик подписки на все ответы от устройств:
  //   assistants/+/response/# — "+" = любой deviceId, "#" = любая команда
  private static final String RESPONSE_TOPIC = "assistants/+/response/#";

  MqttClient client;

  MqttManager() {}

  @EventListener(ApplicationReadyEvent.class)
  public void init() {
    try {
      client = new MqttClient(BROKER_URL, CLIENT_ID);

      MqttConnectOptions options = new MqttConnectOptions();
      options.setCleanSession(true);

      client.setCallback(new MqttCallback() {

        @Override
        public void connectionLost(Throwable cause) {
          System.err.println("MQTT соединение потеряно: " + cause.getMessage());
        }

        @Override
        public void messageArrived(String topic, MqttMessage message) {
          String payload = new String(message.getPayload());
          System.out.println("MQTT получено [" + topic + "]: " + payload);

          try {
            JsonNode json = objectMapper.readTree(payload);
            JsonNode ridNode = json.get("requestId");
            if (ridNode == null || ridNode.isNull()) return;

            String requestId = ridNode.asText();
            DeferredResult<String> pending = responseMap.remove(requestId);

            if (pending != null) {
              pending.setResult(payload);
            } else {
              System.out.println("requestId " + requestId + " не найден (уже отработан или таймаут)");
            }
          } catch (Exception e) {
            System.err.println("Ошибка парсинга MQTT ответа: " + e.getMessage());
          }
        }

        @Override
        public void deliveryComplete(IMqttDeliveryToken token) {}
      });

      System.out.println("Подключение к брокеру...");
      client.connect(options);
      client.subscribe(RESPONSE_TOPIC, 1);
      System.out.println("Клиент подключен: " + client.isConnected() + ", подписка: " + RESPONSE_TOPIC);

    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  public void sendMessage(String topic, String content) {
    MqttMessage message = new MqttMessage(content.getBytes());
    message.setQos(2);
    try {
      client.publish(topic, message);
    } catch (MqttException e) {
      throw new RuntimeException(e);
    }
  }
}
