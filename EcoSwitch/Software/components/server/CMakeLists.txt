idf_component_register(SRCS "server.c"
                    PRIV_REQUIRES
                    esp_http_server
                    switch
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES
                    # Pagina principal
                    ${PROJECT_DIR}/app/main/index.html
                    ${PROJECT_DIR}/app/settings/config.html
                    # Iconos
                    ${PROJECT_DIR}/app/logo.ico)