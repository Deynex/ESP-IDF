idf_component_register(SRCS "nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES nvs_flash)
