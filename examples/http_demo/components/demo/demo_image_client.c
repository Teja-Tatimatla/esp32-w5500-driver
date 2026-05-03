#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"

#include "demo_priv.h"

static const char* TAG = "demo_image";

static const char* DEMO_IMAGE_URLS[] = {
  "http://fastly.picsum.photos/id/29/4000/2670.jpg?hmac=rCbRAl24FzrSzwlR5tL-Aqzyu5tX_PA95VJtnUXegGU",
  "http://fastly.picsum.photos/id/28/4928/3264.jpg?hmac=GnYF-RnBUg44PFfU5pcw_Qs0ReOyStdnZ8MtQWJqTfA",
  "http://fastly.picsum.photos/id/237/3500/2095.jpg?hmac=y2n_cflHFKpQwLOL1SSCtVDqL8NmOnBzEW7LYKZ-z_o",
  "http://fastly.picsum.photos/id/235/5000/3333.jpg?hmac=i9YaRj_AF62lGVYNlYhdL2gqRDxoUzypXLUXBj8ihCc",
  "http://fastly.picsum.photos/id/350/5000/3338.jpg?hmac=Mi1x9fXFZlIsD8MQ3MpQsJmqZhF9vULz9qf6lmNnvUI",
  "http://fastly.picsum.photos/id/556/5000/3333.jpg?hmac=3OTX-0AU9J26J1kYVIcJjDFGrAK5EMz-LRIu4zTzIsI",
};

#define DEMO_IMAGE_URL_COUNT (sizeof(DEMO_IMAGE_URLS) / sizeof(DEMO_IMAGE_URLS[0]))
#define DEMO_IMAGE_READ_BUFFER_SIZE 2048

static const char*
demo_image_pick_random_url(void) {
  uint32_t random_value = esp_random();
  size_t index = random_value % DEMO_IMAGE_URL_COUNT;
  return DEMO_IMAGE_URLS[index];
}

esp_err_t
demo_image_client_stream_jpeg(void* req, uint64_t* bytes_streamed) {
  if (req == NULL || bytes_streamed == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *bytes_streamed = 0;

  httpd_req_t* http_req = (httpd_req_t *)req;
  const char* image_url = demo_image_pick_random_url();

  uint8_t* buffer = malloc(DEMO_IMAGE_READ_BUFFER_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "image buffer allocation failed");
    return ESP_ERR_NO_MEM;
  }

  esp_http_client_config_t config = {
    .url = image_url,
    .method = HTTP_METHOD_GET,
    .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    free(buffer);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "image HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    free(buffer);
    return err;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);

  if (status_code < 200 || status_code >= 300) {
    ESP_LOGE(TAG, "image HTTP status failed: status=%d content_length=%d", status_code, content_length);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(buffer);
    return ESP_ERR_INVALID_RESPONSE;
  }

  httpd_resp_set_hdr(http_req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(http_req, "Pragma", "no-cache");
  httpd_resp_set_type(http_req, "image/jpeg");

  while (true) {
    int bytes_read = esp_http_client_read(client, (char *)buffer, DEMO_IMAGE_READ_BUFFER_SIZE);

    if (bytes_read < 0) {
      ESP_LOGE(TAG, "image HTTP read failed");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      free(buffer);
      return ESP_FAIL;
    }

    if (bytes_read == 0) {
      break;
    }

    err = httpd_resp_send_chunk(http_req, (const char *)buffer, bytes_read);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "image stream to browser failed: %s", esp_err_to_name(err));
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      free(buffer);
      return err;
    }

    *bytes_streamed += (uint64_t)bytes_read;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  free(buffer);

  err = httpd_resp_send_chunk(http_req, NULL, 0);
  if (err != ESP_OK) {
    return err;
  }

  ESP_LOGI(TAG, "streamed image over W5500: %llu bytes", (unsigned long long)*bytes_streamed);
  return ESP_OK;
}
