#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "hidwifi.h"

static const char *TAG = "hidwifi";
static hidwifi_config_t g_cfg;
static TaskHandle_t hidwifi_task_handle = NULL;
static int server = -1;

static int recv_all(int sock, void *buf, size_t len) {
    uint8_t *p = buf;
    while (len > 0) {
        int r = recv(sock, p, len, 0);
        if (r <= 0) return r;
        p += r;
        len -= r;
    }
    return 1;
}


static void hidwifi_task(void *arg) {
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(g_cfg.port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed");
        close(server);
        vTaskDelete(NULL);
        return;
    }

    listen(server, 1);
    ESP_LOGI(TAG, "Listening on port %u", g_cfg.port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client = accept(server, (struct sockaddr *)&client_addr, &len);
        if (client < 0) break;

        ESP_LOGI(TAG, "Client connected");

        while (1) {
            uint8_t hdr[4];
            if (recv_all(client, hdr, 4) <= 0) break;

            uint8_t type = hdr[0];
            uint8_t plen = hdr[1];

            uint8_t buf[32];
            if (plen > sizeof(buf)) break;

            if (recv_all(client, buf, plen) <= 0) break;

            switch (type) {
                case 0x01: // keyboard
                    if (g_cfg.keyboard_cb)
                        g_cfg.keyboard_cb(buf, plen);
                    break;

                case 0x02: // mouse
                    if (g_cfg.mouse_cb)
                        g_cfg.mouse_cb(buf, plen);
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown HID type %u", type);
                    break;
            }
        }

        ESP_LOGI(TAG, "Client disconnected");
        close(client);
    }
    ESP_LOGI(TAG, "hidwifi_task terminates");
	hidwifi_task_handle = NULL;
	vTaskDelete(NULL);
}

static void hidwifi_server_stop(void)
{
    if (server >= 0) {
        close(server);   // unblock accept()
        server = -1;
    }

    ESP_LOGI(TAG, "HID WiFi server stopped");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_START, connecting…");
        esp_wifi_connect();
    } 
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {
		ESP_LOGW(TAG, "WiFi fully stopped — restarting for HID service");

		// Give the WiFi driver time to fully stop
		vTaskDelay(pdMS_TO_TICKS(10000));
		esp_wifi_start();		
		esp_wifi_set_ps(WIFI_PS_NONE);
	}	
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying…");
		hidwifi_server_stop();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGW(TAG, "Got IP address, starting server…");
	    xTaskCreate(hidwifi_task, "hidwifi", 4096, NULL, 5, &hidwifi_task_handle);
    }
}

static void hidwifi_wifi_connect(void)
{
    // Register handlers (safe even if already registered)
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    // Load stored credentials
    wifi_config_t wifi_cfg = {0};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    if (err != ESP_OK || wifi_cfg.sta.ssid[0] == 0) {
        ESP_LOGE(TAG, "No stored WiFi credentials in NVS");
        return;
    }

    ESP_LOGI(TAG, "Using stored SSID: %s", wifi_cfg.sta.ssid);

    // Apply config (safe even if already applied)
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    // Start WiFi if not already running
    esp_wifi_start();
	esp_wifi_set_ps(WIFI_PS_NONE);
}

void hidwifi_start(const hidwifi_config_t *cfg)
{
    g_cfg = *cfg;
	
    hidwifi_wifi_connect();
}
