idf_component_register(SRCS "soft_ap.c"
                    REQUIRES
                        nvs_flash
                        esp_wifi
                    INCLUDE_DIRS "include")
