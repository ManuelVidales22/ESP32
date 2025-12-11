#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// === CONFIGURACIÓN DE PINES ===
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// === CONFIGURACIÓN WIFI ===
const char* ssid = "redmi-note";
const char* password = "124abcjeje";
#define RASPBERRY_SERVER "http://10.95.16.191:5000"

unsigned long lastCapture = 0;
unsigned long interval = 300000; // 5 minutos por defecto

void setupCamera() {
  camera_config_t config;
  
  // Configuración con tus pines que funcionan
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;      // 11
  config.pin_d1 = Y3_GPIO_NUM;      // 9
  config.pin_d2 = Y4_GPIO_NUM;      // 8
  config.pin_d3 = Y5_GPIO_NUM;      // 10
  config.pin_d4 = Y6_GPIO_NUM;      // 12
  config.pin_d5 = Y7_GPIO_NUM;      // 18
  config.pin_d6 = Y8_GPIO_NUM;      // 17
  config.pin_d7 = Y9_GPIO_NUM;      // 16
  config.pin_xclk = XCLK_GPIO_NUM;  // 15
  config.pin_pclk = PCLK_GPIO_NUM;  // 13
  config.pin_vsync = VSYNC_GPIO_NUM;// 6
  config.pin_href = HREF_GPIO_NUM;  // 7
  config.pin_sccb_sda = SIOD_GPIO_NUM;  // 4
  config.pin_sccb_scl = SIOC_GPIO_NUM;  // 5
  config.pin_pwdn = PWDN_GPIO_NUM;  // -1
  config.pin_reset = RESET_GPIO_NUM;// -1
  
  config.xclk_freq_hz = 10000000;  // 10MHz, más bajo para estabilidad
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Configuración para ESP32-S3 sin PSRAM
  config.frame_size = FRAMESIZE_VGA;  // 640x480 (más estable que SVGA sin PSRAM)
  config.jpeg_quality = 12;           // Calidad media-baja
  config.fb_count = 1;                // Solo un buffer
  
  // CONFIGURACIÓN CRÍTICA: Usar DRAM, no PSRAM
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_DRAM;  // ¡IMPORTANTE! Sin PSRAM
  
  Serial.println("Inicializando cámara con pines conocidos...");
  
  // Inicializar cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    
    // Intenta con configuración más reducida
    Serial.println("Probando con resolución más baja...");
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 15;
    config.xclk_freq_hz = 8000000;  // 8MHz
    
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("Configuración alternativa también falló: 0x%x\n", err);
      Serial.println("La cámara no funcionará, pero continuamos con WiFi...");
      return;
    }
  }
  
  Serial.println("Cámara inicializada correctamente");
  
  // Obtener información del sensor
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    Serial.printf("Sensor detectado: PID=%04X, VER=%04X\n", 
                  s->id.PID, s->id.VER);
    
    // Configuración básica del sensor para mejor calidad
    s->set_brightness(s, 0);      // Brillo
    s->set_contrast(s, 0);        // Contraste
    s->set_saturation(s, 0);      // Saturación
    s->set_whitebal(s, 1);        // Balance de blancos automático
    s->set_gain_ctrl(s, 1);       // Control de ganancia automático
    s->set_exposure_ctrl(s, 1);   // Control de exposición automático
    s->set_hmirror(s, 0);         // Espejo horizontal
    s->set_vflip(s, 0);           // Volteo vertical
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== ESP32-S3 Camera con pines conocidos ===");
  
  // Primero inicializar la cámara
  setupCamera();
  
  Serial.println("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFallo en conexión WiFi");
    // Continuamos sin WiFi
  }
}

bool query_fast_mode() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no conectado, modo normal");
    return false;
  }
  
  HTTPClient http;
  String url = String(RASPBERRY_SERVER) + "/mode";
  http.begin(url);
  http.setTimeout(5000);
  
  int code = http.GET();
  bool result = false;
  
  if (code == 200) {
    String s = http.getString();
    result = (s.indexOf("true") >= 0);
    Serial.printf("Modo rápido: %s\n", result ? "activado" : "desactivado");
  } else {
    Serial.printf("Error al consultar modo: %d\n", code);
  }
  
  http.end();
  return result;
}

void send_photo(uint8_t *buf, size_t len) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no conectado, no se puede enviar foto");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(RASPBERRY_SERVER) + "/upload";
  http.begin(client, url);

  String boundary = "------------------------abcd1234";
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  // Partes del multipart
  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"image\"; filename=\"cam.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  // Calcular longitud total
  size_t totalLength = head.length() + len + tail.length();
  
  // Crear el cuerpo completo del mensaje
  uint8_t *fullBody = (uint8_t*)malloc(totalLength);
  if (!fullBody) {
    Serial.println("Error: no se pudo asignar memoria para el cuerpo");
    http.end();
    return;
  }
  
  // Copiar todas las partes al buffer
  memcpy(fullBody, head.c_str(), head.length());
  memcpy(fullBody + head.length(), buf, len);
  memcpy(fullBody + head.length() + len, tail.c_str(), tail.length());
  
  // Enviar POST con todos los datos
  int httpResponseCode = http.POST(fullBody, totalLength);
  
  // Liberar memoria
  free(fullBody);
  
  if (httpResponseCode > 0) {
    Serial.printf("POST enviado. Código de respuesta: %d\n", httpResponseCode);
    
    if (httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("Respuesta del servidor:");
      Serial.println(response);
    }
  } else {
    Serial.printf("Error en POST: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

void loop() {
  // Verificar WiFi cada 30 segundos
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconectando WiFi...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      delay(2000);
    }
    lastWifiCheck = millis();
  }
  
  // Consultar modo (solo si hay WiFi)
  bool fast = false;
  if (WiFi.status() == WL_CONNECTED) {
    fast = query_fast_mode();
  }
  
  // Ajustar intervalo
  interval = fast ? 10000 : 300000; // 10s o 5min
  
  unsigned long now = millis();
  if (now - lastCapture >= interval) {
    Serial.println("Intentando capturar foto...");
    
    // Verificar si la cámara está inicializada
    if (esp_camera_sensor_get() == NULL) {
      Serial.println("Cámara no disponible");
    } else {
      // Tomar foto
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Error al capturar foto");
      } else {
        Serial.printf("Foto capturada: %d bytes\n", fb->len);
        
        if (fb->len > 100) {
          // Mostrar información de la foto
          Serial.printf("Ancho: %d, Alto: %d, Formato: %d\n", 
                       fb->width, fb->height, fb->format);
          send_photo(fb->buf, fb->len);
        } else {
          Serial.println("Foto demasiado pequeña, posible error");
          Serial.printf("Bytes recibidos: %d\n", fb->len);
        }
        
        esp_camera_fb_return(fb);
      }
    }
    lastCapture = now;
  }
  
  delay(1000);
}