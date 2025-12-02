#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
// IP y ruta del servidor en la Raspberry Pi
#define RASPBERRY_SERVER "http://10.70.100.191:5000/upload"

unsigned long lastSend = 0;
const unsigned long sendInterval = 10000; // 10 segundos

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
const char* ssid = "redmi-note";      // CAMBIA por tu WiFi
const char* password = "124abcjeje";   // CAMBIA por tu password

WebServer server(80);

// DECLARAR LAS FUNCIONES ANTES de setup()
void handleRoot();
void handleCapture();
void handleStream();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("🚀 Iniciando cámara ESP32-S3...");

  // Configuración de cámara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;  // 160x120 (funciona sin PSRAM)
  config.jpeg_quality = 10;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;  // Sin PSRAM

  // Inicializar cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Error cámara: 0x%x\n", err);
    return;
  }

  Serial.println("✅ Cámara inicializada!");

  // Conectar WiFi
  WiFi.begin(ssid, password);
  Serial.print("📡 Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi conectado!");
  Serial.print("🌐 IP: ");
  Serial.println(WiFi.localIP());

  // Configurar rutas del servidor
  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/stream", HTTP_GET, handleStream);
  
  server.begin();
  Serial.println("🚀 Servidor web iniciado!");
  Serial.println("📸 Ve a http://" + WiFi.localIP().toString() + " en tu navegador");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>ESP32-CAM</title>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body { font-family: Arial; text-align: center; margin: 20px; }";
  html += "button { padding: 10px 20px; font-size: 16px; margin: 10px; }";
  html += "img { max-width: 90%; border: 2px solid #333; margin: 10px; }";
  html += "</style></head><body>";
  html += "<h1>📸 ESP32-S3 Cámara</h1>";
  html += "<button onclick='capture()'>📷 Capturar Foto</button>";
  html += "<button onclick='stream()'>🎥 Ver Stream</button>";
  html += "<div id='image'></div>";
  html += "<script>";
  html += "function capture() {";
  html += "  document.getElementById('image').innerHTML = '<img src=\"/capture\" />';";
  html += "}";
  html += "function stream() {";
  html += "  document.getElementById('image').innerHTML = '<img src=\"/stream\" />';";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleCapture() {
  Serial.println("📸 Capturando foto...");
  camera_fb_t *fb = esp_camera_fb_get();
  
  if (!fb) {
    server.send(500, "text/plain", "Error capturando foto");
    return;
  }
  
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  Serial.printf("✅ Foto enviada: %u bytes\n", fb->len);
  
  esp_camera_fb_return(fb);
}

void handleStream() {
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  
  server.sendContent(response);
  
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) break;
    
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    server.sendContent_P((const char*)fb->buf, fb->len);
    server.sendContent("\r\n");
    
    esp_camera_fb_return(fb);
    delay(100);
  }
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastSend > sendInterval) {
    lastSend = now;
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      if ((WiFi.status() == WL_CONNECTED)) {
        HTTPClient http;
        http.begin(RASPBERRY_SERVER);
        http.addHeader("Content-Type", "image/jpeg");
        int httpResponseCode = http.POST(fb->buf, fb->len);
        if (httpResponseCode > 0) {
          Serial.printf("✅ Imagen enviada a Raspberry Pi! Código: %d\n", httpResponseCode);
        } else {
          Serial.printf("❌ Error enviando imagen: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
      } else {
        Serial.println("❌ WiFi no conectado, no se pudo enviar la imagen");
      }
      esp_camera_fb_return(fb);
    } else {
      Serial.println("❌ Error capturando foto para enviar");
    }
  }
  delay(2);
}