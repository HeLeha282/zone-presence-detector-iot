package by.alexeiop.restapiassistant;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

@Service
public class ManagementAssistantService {

  private static final Logger logger = LoggerFactory.getLogger(ManagementAssistantService.class);
  MqttManager mqttManager;

  @Autowired
  ManagementAssistantService(MqttManager mqttManager) {
    this.mqttManager = mqttManager;
  }

  public String takePhoto(long id, String requestId){
    logger.info("execute takePhoto");
    mqttManager.sendMessage(
        String.format("assistants/%d/commands", id),
        String.format("{command: \"take_photo\",\nrequestId: %s\n}", requestId));
    return "Фотка доступна по ссылке: https://chat.qwen.ai/";
  }

}
