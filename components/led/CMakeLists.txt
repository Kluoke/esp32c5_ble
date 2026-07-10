idf_component_register(
    SRCS "src/led.c"
    INCLUDE_DIRS "include"
    REQUIRES led_strip esp_driver_gpio
)
