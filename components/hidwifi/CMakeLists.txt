idf_component_register(
    SRCS "src/hidwifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif lwip nvs_flash freertos
)
