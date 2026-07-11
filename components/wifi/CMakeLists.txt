idf_component_register(
    SRCS "src/wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES bt led esp_wifi
)
