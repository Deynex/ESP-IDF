idf_component_register(SRCS "brushless.c"
                    REQUIRES
                        esp_driver_gpio
                        esp_driver_ledc
                    INCLUDE_DIRS "include")
