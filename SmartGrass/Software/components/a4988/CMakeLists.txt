idf_component_register(SRCS "a4988.c"
                    REQUIRES esp_driver_gpio
                             esp_driver_ledc
                    INCLUDE_DIRS "include")
