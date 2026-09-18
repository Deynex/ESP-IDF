idf_component_register(SRCS "server.c"
                    REQUIRES
                        esp_http_server
                        ws
                        vehicle
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES
                    # Pagina principal
                    ${PROJECT_DIR}/app/index.html
                    # Iconos
                    ${PROJECT_DIR}/app/logo.ico)