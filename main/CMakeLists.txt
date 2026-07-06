idf_component_register(SRCS "main.c" "led.c" "btn.c" "ble.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio led_strip bt nvs_flash)