package by.alexeiop.restapiassistant;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.ResourceHandlerRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

@Configuration
public class WebConfig implements WebMvcConfigurer {
  @Override
  public void addResourceHandlers(ResourceHandlerRegistry registry) {
    registry.addResourceHandler("/photo_*.jpg") // Слушать файлы, начинающиеся на photo_
        .addResourceLocations("file:photos/"); // Искать в реальной папке photos/ (проверьте имя папки!)
  }
}