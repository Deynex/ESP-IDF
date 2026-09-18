idf_component_register(SRCS "switch.c"
                    PRIV_REQUIRES
                    esp_driver_gpio
                    esp_adc
                    INCLUDE_DIRS "include")
