# CMakeLists.txt
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES esp_event esp_driver_gpio esp_http_server esp_wifi nvs_flash esp_netif esp_timer esp-tls
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES
                    ${project_dir}/web\ pages/main.html
                    ${project_dir}/web\ pages/router.ico)

# Imprime el directorio del proyecto y las rutas de los archivos HTML
message(STATUS "Project directory: ${project_dir}")
message(STATUS "Main HTML path: ${project_dir}/web pages/main.html")
message(STATUS "Router ICO path: ${project_dir}/web pages/router.ico")