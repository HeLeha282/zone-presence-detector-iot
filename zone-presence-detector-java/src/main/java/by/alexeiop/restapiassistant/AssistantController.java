package by.alexeiop.restapiassistant;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.context.request.async.DeferredResult;

import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;

/**
 * REST-контроллер для управления ESP32-ассистентом.
 * Все команды публикуются в MQTT и ждут ответа через DeferredResult.
 *
 * Топики:
 *   команды  → assistants/{id}/commands/{command}
 *   ответы   ← assistants/{id}/response/{command}   (обрабатывает MqttManager)
 */
@RestController
@RequestMapping("api/v1/assistants")
public class AssistantController {

    private static final Logger logger = LoggerFactory.getLogger(AssistantController.class);

    // Таймаут ожидания ответа от ESP32
    private static final long TIMEOUT_MS       = 10_000L;
    // Для фото — длиннее: устройству нужно сделать снимок и загрузить его
    private static final long PHOTO_TIMEOUT_MS = 100_000L;

    private final AssistantService assistantService;
    private final MqttManager mqttManager;

    @Autowired
    AssistantController(AssistantService assistantService, MqttManager mqttManager) {
        this.assistantService = assistantService;
        this.mqttManager = mqttManager;
    }

    // ── хелпер ────────────────────────────────────────────────────────────────

    /**
     * Создаёт DeferredResult, регистрирует его в responseMap по сгенерированному
     * requestId и вызывает sender(requestId) для публикации MQTT-команды.
     * Если таймаут истёк — возвращает JSON с ошибкой.
     */
    private DeferredResult<String> mqttCommand(long timeoutMs, Consumer<String> sender) {
        String requestId = UUID.randomUUID().toString();
        DeferredResult<String> result = new DeferredResult<>(timeoutMs);

        result.onTimeout(() -> {
            mqttManager.responseMap.remove(requestId);
            result.setErrorResult("{\"status\":\"error\",\"message\":\"Timeout: устройство не отвечает\"}");
        });

        mqttManager.responseMap.put(requestId, result);

        try {
            sender.accept(requestId);
        } catch (Exception e) {
            mqttManager.responseMap.remove(requestId);
            result.setErrorResult("{\"status\":\"error\",\"message\":\"" + e.getMessage() + "\"}");
        }

        return result;
    }

    private DeferredResult<String> mqttCommand(Consumer<String> sender) {
        return mqttCommand(TIMEOUT_MS, sender);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  КОНФИГУРАЦИЯ
    // ═══════════════════════════════════════════════════════════════════════════

    /** GET /api/v1/assistants/{id}/config — получить текущую конфигурацию */
    @GetMapping("/{id}/config")
    public DeferredResult<String> getConfig(@PathVariable long id) {
        logger.info("getConfig id={}", id);
        return mqttCommand(rid -> assistantService.getConfig(id, rid));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  ТОКЕНЫ
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * POST /api/v1/assistants/{id}/config/tokens
     * Body: {"tokenId":"home","token":"<uuid>"}
     */
    @PostMapping("/{id}/config/tokens")
    public DeferredResult<String> addToken(@PathVariable long id,
                                           @RequestBody Map<String, String> body) {
        logger.info("addToken id={} tokenId={}", id, body.get("tokenId"));
        return mqttCommand(rid ->
                assistantService.addToken(id, rid, body.get("tokenId"), body.get("token")));
    }

    /**
     * DELETE /api/v1/assistants/{id}/config/tokens/{tokenId}
     */
    @DeleteMapping("/{id}/config/tokens/{tokenId}")
    public DeferredResult<String> removeToken(@PathVariable long id,
                                              @PathVariable String tokenId) {
        logger.info("removeToken id={} tokenId={}", id, tokenId);
        return mqttCommand(rid -> assistantService.removeToken(id, rid, tokenId));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  СЦЕНЫ
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * POST /api/v1/assistants/{id}/config/scenes
     * Body: {"sceneKey":"lights_on","sceneId":"<uuid>","tokenId":"home"}
     */
    @PostMapping("/{id}/config/scenes")
    public DeferredResult<String> addScene(@PathVariable long id,
                                           @RequestBody Map<String, String> body) {
        logger.info("addScene id={} sceneKey={}", id, body.get("sceneKey"));
        return mqttCommand(rid ->
                assistantService.addScene(id, rid,
                        body.get("sceneKey"), body.get("sceneId"), body.get("tokenId")));
    }

    /**
     * DELETE /api/v1/assistants/{id}/config/scenes/{sceneKey}
     */
    @DeleteMapping("/{id}/config/scenes/{sceneKey}")
    public DeferredResult<String> removeScene(@PathVariable long id,
                                              @PathVariable String sceneKey) {
        logger.info("removeScene id={} sceneKey={}", id, sceneKey);
        return mqttCommand(rid -> assistantService.removeScene(id, rid, sceneKey));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  ПРИВЯЗКИ ЗОН К СЦЕНАМ
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * POST /api/v1/assistants/{id}/config/bindings
     * Body: {"zoneName":"My_Table","event":"enter","sceneKey":"lights_on","delayMs":0}
     */
    @PostMapping("/{id}/config/bindings")
    public DeferredResult<String> addBinding(@PathVariable long id,
                                             @RequestBody Map<String, Object> body) {
        String zoneName  = (String) body.get("zoneName");
        String event     = (String) body.get("event");
        String sceneKey  = (String) body.get("sceneKey");
        long   delayMs   = body.get("delayMs") instanceof Number n ? n.longValue() : 0L;
        logger.info("addBinding id={} zone={} event={} scene={} delay={}", id, zoneName, event, sceneKey, delayMs);
        return mqttCommand(rid ->
                assistantService.bindZone(id, rid, zoneName, event, sceneKey, delayMs));
    }

    /**
     * DELETE /api/v1/assistants/{id}/config/bindings/{zoneName}/{event}/{sceneKey}
     * Тело не нужно — все параметры в пути URL.
     */
    @DeleteMapping("/{id}/config/bindings/{zoneName}/{event}/{sceneKey}")
    public DeferredResult<String> removeBinding(@PathVariable long id,
                                                @PathVariable String zoneName,
                                                @PathVariable String event,
                                                @PathVariable String sceneKey) {
        logger.info("removeBinding id={} zone={} event={} scene={}", id, zoneName, event, sceneKey);
        return mqttCommand(rid ->
                assistantService.unbindZone(id, rid, zoneName, event, sceneKey));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  ЗОНЫ ESP32
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * GET /api/v1/assistants/{id}/zones — получить все зоны с устройства
     */
    @GetMapping("/{id}/zones")
    public DeferredResult<String> getZones(@PathVariable long id) {
        logger.info("getZones id={}", id);
        return mqttCommand(rid -> assistantService.getZones(id, rid));
    }

    /**
     * POST /api/v1/assistants/{id}/zones — добавить или обновить зону
     * Body: {"zoneName":"My_Table","vertices":[{"x":100,"y":200},{"x":300,"y":400}]}
     */
    @PostMapping("/{id}/zones")
    public DeferredResult<String> addZone(@PathVariable long id,
                                          @RequestBody Map<String, Object> body) {
        String zoneName = (String) body.get("zoneName");
        Object vertices = body.get("vertices");
        logger.info("setZone id={} zone={}", id, zoneName);
        return mqttCommand(rid -> assistantService.setZone(id, rid, zoneName, vertices));
    }

    /**
     * DELETE /api/v1/assistants/{id}/zones — удалить все зоны на устройстве
     */
    @DeleteMapping("/{id}/zones")
    public DeferredResult<String> clearZones(@PathVariable long id) {
        logger.info("clearZones id={}", id);
        return mqttCommand(rid -> assistantService.clearZones(id, rid));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    //  ДЕЙСТВИЯ
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * POST /api/v1/assistants/{id}/actions/take-photo
     * Body: {"requestId":"<uuid>"} — requestId опционален; если не указан, генерируется здесь.
     *
     * Ответ приходит через HTTP: ESP32 загружает JPEG на
     *   POST /api/management_assistant/upload-photo/{requestId}
     * Поэтому DeferredResult резолвится не через MQTT-callback, а в ManagementAssistantController.
     */
    @PostMapping("/{id}/actions/take-photo")
    public DeferredResult<String> takePhoto(@PathVariable long id,
                                            @RequestBody(required = false) Map<String, String> body) {
        // Если клиент прислал свой requestId — используем его (для трассировки)
        String clientRid = (body != null) ? body.get("requestId") : null;

        String requestId = (clientRid != null && !clientRid.isBlank()) ? clientRid : UUID.randomUUID().toString();

        DeferredResult<String> result = new DeferredResult<>(PHOTO_TIMEOUT_MS);
        result.onTimeout(() -> {
            mqttManager.responseMap.remove(requestId);
            result.setErrorResult("{\"status\":\"error\",\"message\":\"Timeout: устройство не отвечает\"}");
        });

        mqttManager.responseMap.put(requestId, result);

        try {
            assistantService.takePhoto(id, requestId);
            logger.info("takePhoto id={} requestId={}", id, requestId);
        } catch (Exception e) {
            mqttManager.responseMap.remove(requestId);
            result.setErrorResult("{\"status\":\"error\",\"message\":\"" + e.getMessage() + "\"}");
        }

        return result;
    }
}
