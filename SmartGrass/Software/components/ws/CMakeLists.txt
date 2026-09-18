idf_component_register(SRCS "ws.c"
                    REQUIRES
                        esp_http_server
                        a4988
                        vehicle
                        brushless
                    INCLUDE_DIRS "include")