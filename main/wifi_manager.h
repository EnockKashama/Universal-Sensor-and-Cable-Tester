#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define PORTAL_AP_SSID "ESP32_CableTester"
#define PORTAL_AP_PASS ""

static EventGroupHandle_t s_wifi_event_group = NULL;
static const char *WIFI_TAG = "wifi";
static bool s_portal_running = false;
static bool s_portal_connected = false;
static httpd_handle_t s_portal_server = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_wifi_event_group)
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    if (s_wifi_event_group)
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    s_portal_connected = true;
    s_portal_running = false;
  }
}

inline esp_err_t wifi_init() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler,
                             NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler,
                             NULL);

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  return ESP_OK;
}

inline bool wifi_is_connected() {
  if (!s_wifi_event_group)
    return false;
  return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

inline esp_err_t wifi_connect_sta(const char *ssid, const char *password) {
  if (!s_wifi_event_group)
    s_wifi_event_group = xEventGroupCreate();
  else
    xEventGroupClearBits(s_wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, password,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  esp_wifi_disconnect();
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // This saves to NVS
  esp_wifi_connect();

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(15000));

  if (bits & WIFI_CONNECTED_BIT)
    return ESP_OK;
  return ESP_FAIL;
}

// ============================================================
//                    CAPTIVE PORTAL
// ============================================================
static char s_portal_ssid[64] = "";
static char s_portal_pass[64] = "";

static const char *PORTAL_HTML =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width'>"
    "<title>Cable Tester WiFi Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px "
    "auto;padding:20px;}"
    "input{width:100%;padding:8px;margin:8px 0;box-sizing:border-box;}"
    "button{width:100%;padding:10px;background:#00FFC8;border:none;font-size:"
    "16px;cursor:pointer;}"
    "h2{color:#1A1A2E;}</style></head>"
    "<body><h2>Cable Tester WiFi Setup</h2>"
    "<form method='POST' action='/connect'>"
    "<label>WiFi SSID:</label>"
    "<input type='text' name='ssid' placeholder='Enter WiFi name'><br>"
    "<label>Password:</label>"
    "<input type='password' name='pass' placeholder='Enter WiFi password'><br>"
    "<button type='submit'>Connect</button>"
    "</form></body></html>";

static const char *PORTAL_SUCCESS_HTML =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width'>"
    "<title>Connecting...</title></head>"
    "<body><h2>Connecting to WiFi...</h2>"
    "<p>The device is now connecting. This page will close.</p>"
    "<p>You can reconnect to your normal WiFi network.</p>"
    "</body></html>";

static esp_err_t portal_get_handler(httpd_req_t *req) {
  httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static void url_decode(char *dst, const char *src, int dst_size) {
  char *d = dst;
  const char *s = src;
  int remaining = dst_size - 1;
  while (*s && remaining > 0) {
    if (*s == '%' && *(s + 1) && *(s + 2)) {
      char hex[3] = {*(s + 1), *(s + 2), 0};
      *d++ = (char)strtol(hex, NULL, 16);
      s += 3;
    } else if (*s == '+') {
      *d++ = ' ';
      s++;
    } else {
      *d++ = *s++;
    }
    remaining--;
  }
  *d = '\0';
}

static esp_err_t portal_post_handler(httpd_req_t *req) {
  char buf[256] = {};
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
    return ESP_FAIL;

  printf("Portal POST body: %s\n", buf);

  char raw_ssid[64] = "";
  char raw_pass[64] = "";

  char *ssid_ptr = strstr(buf, "ssid=");
  char *pass_ptr = strstr(buf, "pass=");

  if (ssid_ptr) {
    ssid_ptr += 5;
    char *end = strchr(ssid_ptr, '&');
    if (end) {
      strncpy(raw_ssid, ssid_ptr, end - ssid_ptr);
    } else {
      strncpy(raw_ssid, ssid_ptr, sizeof(raw_ssid) - 1);
    }
  }
  if (pass_ptr) {
    pass_ptr += 5;
    char *end = strchr(pass_ptr, '&');
    if (end) {
      strncpy(raw_pass, pass_ptr, end - pass_ptr);
    } else {
      strncpy(raw_pass, pass_ptr, sizeof(raw_pass) - 1);
    }
  }

  url_decode(s_portal_ssid, raw_ssid, sizeof(s_portal_ssid));
  url_decode(s_portal_pass, raw_pass, sizeof(s_portal_pass));

  printf("Portal got SSID: '%s' PASS: '%s'\n", s_portal_ssid, s_portal_pass);

  httpd_resp_send(req, PORTAL_SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);
  s_portal_running = false;
  return ESP_OK;
}

static esp_netif_t *s_ap_netif =
    NULL; // track AP netif to avoid double creation

inline void wifi_start_portal() {
  s_portal_running = true;
  s_portal_connected = false;
  s_portal_ssid[0] = '\0';
  s_portal_pass[0] = '\0';

  // Only create AP netif once
  if (s_ap_netif == NULL) {
    s_ap_netif = esp_netif_create_default_wifi_ap();
  }

  wifi_config_t ap_config = {};
  strncpy((char *)ap_config.ap.ssid, PORTAL_AP_SSID, sizeof(ap_config.ap.ssid));
  ap_config.ap.ssid_len = strlen(PORTAL_AP_SSID);
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;

  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  esp_wifi_start();

  // Start HTTP server only if not already running
  if (s_portal_server == NULL) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&s_portal_server, &config);

    httpd_uri_t get_uri = {.uri = "/",
                           .method = HTTP_GET,
                           .handler = portal_get_handler,
                           .user_ctx = NULL};
    httpd_uri_t post_uri = {.uri = "/connect",
                            .method = HTTP_POST,
                            .handler = portal_post_handler,
                            .user_ctx = NULL};

    httpd_register_uri_handler(s_portal_server, &get_uri);
    httpd_register_uri_handler(s_portal_server, &post_uri);
  }

  ESP_LOGI(WIFI_TAG, "Portal started. SSID: %s IP: 192.168.4.1",
           PORTAL_AP_SSID);
}

inline void wifi_stop_portal() {
  if (s_portal_server) {
    httpd_stop(s_portal_server);
    s_portal_server = NULL;
  }
  // Switch back to STA only mode
  esp_wifi_set_mode(WIFI_MODE_STA);
  s_portal_running = false;
}
inline bool wifi_portal_got_credentials() { return strlen(s_portal_ssid) > 0; }

inline const char *wifi_portal_ssid() { return s_portal_ssid; }
inline const char *wifi_portal_pass() { return s_portal_pass; }
inline bool wifi_portal_running() { return s_portal_running; }

inline void wifi_auto_connect() {
  if (!s_wifi_event_group)
    s_wifi_event_group = xEventGroupCreate();

  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGI(WIFI_TAG, "Auto-connect failed - no saved credentials");
    return;
  }

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(10000));

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(WIFI_TAG, "Auto-connected to WiFi");
  } else {
    ESP_LOGI(WIFI_TAG, "Auto-connect failed - no saved credentials");
  }
}
#endif