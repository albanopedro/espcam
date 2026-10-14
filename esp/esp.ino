// Parte 2: Wi-Fi + servidor web com vídeo ao vivo e foto.
// Depois de conectar, o endereço para abrir no navegador aparece no Serial Monitor (115200 baud).

#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>
#include "camera_pins.h"
#include "wifi_config.h"

httpd_handle_t web_httpd = NULL;     // servidor da porta 80: página e foto
httpd_handle_t stream_httpd = NULL;  // servidor da porta 81: vídeo

// O vídeo é um MJPEG: uma resposta que nunca termina, enviando uma foto JPEG atrás da outra.
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// Página que o navegador recebe ao abrir http://<ip-da-placa>/
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM</title>
  <style>
    body { font-family: sans-serif; background: #111; color: #eee; text-align: center; padding: 16px; }
    img { max-width: 100%; border-radius: 8px; }
    a { color: #6cf; }
  </style>
</head>
<body>
  <h1>ESP32-CAM</h1>
  <img id="video" alt="video ao vivo">
  <p><a href="/capture" target="_blank">Tirar foto</a></p>
  <script>
    // o vídeo vem da porta 81, no mesmo endereço da página
    document.getElementById('video').src = location.protocol + '//' + location.hostname + ':81/stream';
  </script>
</body>
</html>
)rawliteral";

// ---------- Câmera (igual à Parte 1, agora dentro de uma função) ----------

bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = 12;

  if (psramFound()) {
    config.frame_size  = FRAMESIZE_VGA;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count    = 2;
    config.grab_mode   = CAMERA_GRAB_LATEST;  // no vídeo, sempre entrega o quadro mais novo
  } else {
    config.frame_size  = FRAMESIZE_QVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erro ao iniciar a camera: 0x%x\n", err);
    return false;
  }
  return true;
}

// ---------- Wi-Fi ----------

bool connectWifi() {
  WiFi.mode(WIFI_STA);  // modo "estação": a placa entra numa rede que já existe
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); // sem economia de energia do Wi-Fi, o vídeo trava menos

  Serial.print("Conectando ao Wi-Fi");
  for (int tentativa = 0; tentativa < 40 && WiFi.status() != WL_CONNECTED; tentativa++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Nao conectou. Confira nome e senha em wifi_config.h (e se a rede e 2.4 GHz).");
    return false;
  }
  return true;
}

// ---------- Rotas do servidor ----------

// GET /  -> devolve a página HTML
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

// GET /capture  -> devolve uma única foto JPEG
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=foto.jpg");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

// GET /stream  -> vídeo ao vivo: fica enviando fotos até o navegador fechar a conexão
static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  char part_header[64];
  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
    } else {
      size_t header_len = snprintf(part_header, sizeof(part_header), STREAM_PART, (unsigned)fb->len);
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_header, header_len);
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      esp_camera_fb_return(fb);
    }
    if (res != ESP_OK) break;  // o navegador fechou a aba: sai do laço
  }
  return res;
}

// Liga os dois servidores. O vídeo precisa de um servidor só dele porque
// o laço infinito acima ocuparia o servidor inteiro e a página/foto nunca responderiam.
void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri   = { .uri = "/",        .method = HTTP_GET, .handler = index_handler,   .user_ctx = NULL };
  httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };
  httpd_uri_t stream_uri  = { .uri = "/stream",  .method = HTTP_GET, .handler = stream_handler,  .user_ctx = NULL };

  config.server_port = 80;
  if (httpd_start(&web_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(web_httpd, &index_uri);
    httpd_register_uri_handler(web_httpd, &capture_uri);
  }

  config.server_port = 81;
  config.ctrl_port += 1;  // cada servidor precisa da sua porta de controle interna
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (!startCamera()) return;
  Serial.println("Camera iniciada com sucesso!");

  if (!connectWifi()) return;
  startWebServer();

  Serial.print("Pronto! Abra no navegador: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  // Nada a fazer: os servidores rodam sozinhos em tarefas próprias.
  delay(10000);
}
