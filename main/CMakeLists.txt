idf_component_register(SRCS "esp_ot_br.c"
                       PRIV_REQUIRES esp_psram esp_http_server openthread esp_coex esp_wifi nvs_flash
                       INCLUDE_DIRS ".")
