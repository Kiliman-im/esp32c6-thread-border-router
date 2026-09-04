/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Border Router Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_coexist.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "openthread/thread.h"
#include "esp_openthread_spinel.h"
#include "esp_openthread_types.h"
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_config.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_eventfd.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "openthread/dataset.h"
#include "ot_examples_br.h"
#include "ot_examples_common.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
#include "ext_coex_cmd.h"
#endif

#define TAG "esp_ot_br"

/* XIAO ESP32-C6 user LED — GPIO15, active-low */
#define USER_LED_GPIO 15

static void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(USER_LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* 3 quick blinks at boot */
    for (int i = 0; i < 3; i++) {
        gpio_set_level(USER_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(USER_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

static void led_task(void *arg)
{
    bool blink_state = false;
    while (1) {
        otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
        otInstance *instance = esp_openthread_get_instance();
        if (instance) {
            esp_openthread_lock_acquire(portMAX_DELAY);
            role = otThreadGetDeviceRole(instance);
            esp_openthread_lock_release();
        }

        if (role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER) {
            gpio_set_level(USER_LED_GPIO, 0); /* solid on */
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            blink_state = !blink_state;
            gpio_set_level(USER_LED_GPIO, blink_state ? 0 : 1);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

static esp_err_t get_active_dataset_hex(char *buffer, size_t buffer_size)
{
    otOperationalDatasetTlvs dataset;

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    otError error = instance == NULL ? OT_ERROR_INVALID_STATE : otDatasetGetActiveTlvs(instance, &dataset);
    esp_openthread_lock_release();

    if (error != OT_ERROR_NONE) {
        return ESP_FAIL;
    }

    if (buffer_size < (size_t)dataset.mLength * 2 + 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (uint8_t index = 0; index < dataset.mLength; index++) {
        snprintf(buffer + index * 2, buffer_size - index * 2, "%02x", dataset.mTlvs[index]);
    }

    return ESP_OK;
}

static esp_err_t node_handler(httpd_req_t *request)
{
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *instance = esp_openthread_get_instance();
    otDeviceRole role = instance == NULL ? OT_DEVICE_ROLE_DISABLED : otThreadGetDeviceRole(instance);
    esp_openthread_lock_release();

    char response[16];
    int state = role >= OT_DEVICE_ROLE_CHILD ? 4 : 1;
    snprintf(response, sizeof(response), "{\"State\":%d}", state);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t active_dataset_handler(httpd_req_t *request)
{
    char dataset_hex[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 1];
    if (get_active_dataset_hex(dataset_hex, sizeof(dataset_hex)) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "No active dataset");
        return ESP_FAIL;
    }

    char response[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 32];
    snprintf(response, sizeof(response), "{\"ActiveDataset\":\"%s\"}", dataset_hex);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t dataset_handler(httpd_req_t *request)
{
    char dataset_hex[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 1];
    if (get_active_dataset_hex(dataset_hex, sizeof(dataset_hex)) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "No active dataset");
        return ESP_FAIL;
    }

    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, dataset_hex);
}

static void start_rest_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8081;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    static const httpd_uri_t node_uri = {
        .uri = "/node",
        .method = HTTP_GET,
        .handler = node_handler,
    };
    static const httpd_uri_t active_dataset_uri = {
        .uri = "/networks/dataset/active",
        .method = HTTP_GET,
        .handler = active_dataset_handler,
    };
    static const httpd_uri_t dataset_uri = {
        .uri = "/dataset",
        .method = HTTP_GET,
        .handler = dataset_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &node_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &active_dataset_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &dataset_uri));
    ESP_LOGI(TAG, "REST API started on port %d", config.server_port);
}

#if CONFIG_OPENTHREAD_SUPPORT_HW_RESET_RCP
#define PIN_TO_RCP_RESET CONFIG_OPENTHREAD_HW_RESET_RCP_PIN
static void rcp_failure_hardware_reset_handler(void)
{
    gpio_config_t reset_pin_config;
    memset(&reset_pin_config, 0, sizeof(reset_pin_config));
    reset_pin_config.intr_type = GPIO_INTR_DISABLE;
    reset_pin_config.pin_bit_mask = BIT(PIN_TO_RCP_RESET);
    reset_pin_config.mode = GPIO_MODE_OUTPUT;
    reset_pin_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset_pin_config.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&reset_pin_config);
    gpio_set_level(PIN_TO_RCP_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TO_RCP_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_reset_pin(PIN_TO_RCP_RESET);
}
#endif

void app_main(void)
{
    led_init();

    // Used eventfds:
    // * netif
    // * task queue
    // * border router
    size_t max_eventfd = 3;

#if CONFIG_OPENTHREAD_RADIO_NATIVE || CONFIG_OPENTHREAD_RADIO_SPINEL_SPI
    // * radio driver (A native radio device needs a eventfd for radio driver.)
    // * SpiSpinelInterface (The Spi Spinel Interface needs a eventfd.)
    // The above will not exist at the same time.
    max_eventfd++;
#endif
#if CONFIG_OPENTHREAD_RADIO_TREL
    // * TREL reception (The Thread Radio Encapsulation Link needs a eventfd for reception.)
    max_eventfd++;
#endif
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = max_eventfd,
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));
#if CONFIG_OPENTHREAD_SUPPORT_HW_RESET_RCP
    esp_openthread_register_rcp_failure_handler(rcp_failure_hardware_reset_handler);
    esp_openthread_set_coprocessor_reset_failure_callback(rcp_failure_hardware_reset_handler);
#endif

#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
    ot_register_external_commands();
#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
    register_cmd_extcoex();
#endif
#endif

#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
    ot_external_coexist_init();
#endif

    static esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));
    start_rest_server();
    xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#endif
#if CONFIG_OPENTHREAD_BORDER_ROUTER && CONFIG_OPENTHREAD_NETWORK_AUTO_START
    ESP_ERROR_CHECK(esp_openthread_border_router_start());
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_SOC_IEEE802154_SUPPORTED
    ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());
#endif
#endif
}
