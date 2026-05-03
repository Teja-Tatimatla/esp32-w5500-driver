#include "esp_http_server.h"
#include "esp_log.h"

#include "demo.h"
#include "demo_priv.h"

static const char* TAG = "demo_http";

static httpd_handle_t http_server = NULL;

static esp_err_t
demo_status_get_handler(httpd_req_t* req) {
  if (!demo_is_available()) {
    const char* response = "{\"status\":\"error\",\"network\":\"unavailable\"}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, response);
    demo_data_add_bytes(strlen(response));
    return ESP_OK;
  }

  demo_data_snapshot_t snapshot = {0};
  esp_err_t err = demo_data_get_snapshot(&snapshot);
  if (err != ESP_OK) {
    const char* response = "{\"status\":\"error\",\"message\":\"snapshot unavailable\"}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, response);
    demo_data_add_bytes(strlen(response));
    return ESP_OK;
  }

  char response[512] = {0};
  int written = snprintf(
    response,
    sizeof(response),
    "{"
      "\"status\":\"ok\","
      "\"ethernet\":\"%s\","
      "\"ip\":\"%s\","
      "\"driver\":\"%s\","
      "\"last_update_seconds\":%lld,"
      "\"bytes_transferred\":%llu"
    "}",
    snapshot.ethernet_up ? "up" : "down",
    snapshot.ip,
    snapshot.driver,
    (long long)snapshot.last_update_seconds,
    (unsigned long long)snapshot.bytes_transferred
  );

  if (written < 0 || written >= sizeof(response)) {
    const char* error_response = "{\"status\":\"error\",\"message\":\"response too large\"}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, error_response);
    demo_data_add_bytes(strlen(error_response));
    return ESP_OK;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, response);
  demo_data_add_bytes((uint64_t)strlen(response));

  return ESP_OK;
}

static esp_err_t
demo_image_get_handler(httpd_req_t* req) {
  if (!demo_is_available()) {
    const char* response = "{\"status\":\"error\",\"network\":\"unavailable\"}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, response);
    demo_data_add_bytes(strlen(response));
    return ESP_OK;
  }

  uint64_t bytes_streamed = 0;
  esp_err_t err = demo_image_client_stream_jpeg(req, &bytes_streamed);
  if (err != ESP_OK) {
    const char* response = "{\"status\":\"error\",\"message\":\"image fetch failed\"}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "502 Bad Gateway");
    httpd_resp_sendstr(req, response);
    demo_data_add_bytes(strlen(response));
    return ESP_OK;
  }

  demo_data_add_bytes(bytes_streamed);
  return ESP_OK;
}

esp_err_t
demo_http_server_start(void) {
  if (http_server != NULL) {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;

  esp_err_t err = httpd_start(&http_server, &config);
  if (err != ESP_OK) {
    http_server = NULL;
    ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = demo_status_get_handler,
    .user_ctx = NULL,
  };

  err = httpd_register_uri_handler(http_server, &status_uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to register /status handler: %s", esp_err_to_name(err));
    httpd_stop(http_server);
    http_server = NULL;
    return err;
  }

  httpd_uri_t image_uri = {
    .uri = "/image",
    .method = HTTP_GET,
    .handler = demo_image_get_handler,
    .user_ctx = NULL,
  };

  err = httpd_register_uri_handler(http_server, &image_uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to register /image handler: %s", esp_err_to_name(err));
    httpd_stop(http_server);
    http_server = NULL;
    return err;
  }

  ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
  return ESP_OK;
}

esp_err_t
demo_http_server_stop(void) {
  if (http_server == NULL) {
    return ESP_OK;
  }

  esp_err_t err = httpd_stop(http_server);
  http_server = NULL;

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP server stop failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "HTTP server stopped");
  return ESP_OK;
}
