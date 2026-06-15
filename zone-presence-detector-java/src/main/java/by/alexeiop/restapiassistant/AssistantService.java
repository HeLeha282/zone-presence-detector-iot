package by.alexeiop.restapiassistant;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import tools.jackson.databind.ObjectMapper;

import java.util.List;
import java.util.Map;

/**
 * Сервис для новых команд управления ESP32 (токены / сцены / привязки / зоны / фото).
 * Все команды публикуются в топик assistants/{id}/commands/{command}.
 * Ответы приходят через MQTT на assistants/{id}/response/{command} и разрешаются
 * по requestId через MqttManager.responseMap.
 */
@Service
public class AssistantService {

    private static final Logger logger = LoggerFactory.getLogger(AssistantService.class);
    private final MqttManager mqttManager;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Autowired
    AssistantService(MqttManager mqttManager) {
        this.mqttManager = mqttManager;
    }

    // ── внутренний хелпер ─────────────────────────────────────────────────────

    /**
     * Собирает JSON-пейлоад и публикует в MQTT.
     * @param extra дополнительные поля в JSON (начинаются с запятой), например
     *              {@code ,\"tokenId\":\"home\"} — или пустая строка если полей нет
     */
    private void publish(long id, String command, String requestId, String extra) {
        String topic   = String.format("assistants/%d/commands/%s", id, command);
        String payload = String.format("{\"command\":\"%s\",\"requestId\":\"%s\"%s}",
                command, requestId, extra);
        logger.info("MQTT -> [{}]: {}", topic, payload);
        mqttManager.sendMessage(topic, payload);
    }

    /** Экранирует спецсимволы для вставки в JSON-строку вручную. */
    private String esc(String s) {
        return s == null ? "" : s.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    // ── конфигурация ──────────────────────────────────────────────────────────

    public void getConfig(long id, String requestId) {
        publish(id, "get_config", requestId, "");
    }

    // ── токены ────────────────────────────────────────────────────────────────

    public void addToken(long id, String requestId, String tokenId, String token) {
        publish(id, "add_token", requestId,
                String.format(",\"tokenId\":\"%s\",\"token\":\"%s\"", esc(tokenId), esc(token)));
    }

    public void removeToken(long id, String requestId, String tokenId) {
        publish(id, "remove_token", requestId,
                String.format(",\"tokenId\":\"%s\"", esc(tokenId)));
    }

    // ── сцены ─────────────────────────────────────────────────────────────────

    public void addScene(long id, String requestId, String sceneKey, String sceneId, String tokenId) {
        publish(id, "add_scene", requestId,
                String.format(",\"sceneKey\":\"%s\",\"sceneId\":\"%s\",\"tokenId\":\"%s\"",
                        esc(sceneKey), esc(sceneId), esc(tokenId)));
    }

    public void removeScene(long id, String requestId, String sceneKey) {
        publish(id, "remove_scene", requestId,
                String.format(",\"sceneKey\":\"%s\"", esc(sceneKey)));
    }

    // ── привязки ──────────────────────────────────────────────────────────────

    public void bindZone(long id, String requestId,
                         String zoneName, String event, String sceneKey, long delayMs) {
        publish(id, "bind_zone", requestId,
                String.format(",\"zone\":\"%s\",\"event\":\"%s\",\"scene\":\"%s\",\"delay\":%d",
                        esc(zoneName), esc(event), esc(sceneKey), delayMs));
    }

    public void unbindZone(long id, String requestId,
                           String zoneName, String event, String sceneKey) {
        publish(id, "unbind_zone", requestId,
                String.format(",\"zone\":\"%s\",\"event\":\"%s\",\"scene\":\"%s\"",
                        esc(zoneName), esc(event), esc(sceneKey)));
    }

    // ── зоны ESP32 ────────────────────────────────────────────────────────────

    public void getZones(long id, String requestId) {
        publish(id, "get_zones", requestId, "");
    }

    /**
     * Добавляет/обновляет зону. vertices — список вершин, уже десериализованный
     * из тела запроса как {@code List<Map<String,Object>>}.
     */
    public void setZone(long id, String requestId, String zoneName, Object vertices) {
        try {
            String verticesJson = objectMapper.writeValueAsString(vertices);
            publish(id, "set_zone", requestId,
                    String.format(",\"name\":\"%s\",\"vertices\":%s", esc(zoneName), verticesJson));
        } catch (Exception e) {
            throw new RuntimeException("Ошибка сериализации вершин: " + e.getMessage(), e);
        }
    }

    public void clearZones(long id, String requestId) {
        publish(id, "clear_zones", requestId, "");
    }

    // ── действия ──────────────────────────────────────────────────────────────

    /**
     * Команда на съёмку фото. Ответ приходит НЕ через MQTT, а через HTTP:
     * ESP32 загружает JPEG на POST /api/management_assistant/upload-photo/{requestId},
     * где DeferredResult разрешается. Метод здесь только публикует команду.
     */
    public void takePhoto(long id, String requestId) {
        publish(id, "take_photo", requestId, "");
    }
}
