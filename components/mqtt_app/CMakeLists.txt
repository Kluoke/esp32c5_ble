idf_component_register(
    SRCS "src/mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash mqtt
)