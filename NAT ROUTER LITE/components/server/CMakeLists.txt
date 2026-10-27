idf_build_get_property(project_dir PROJECT_DIR)

idf_component_register(
    SRCS "server.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server esp_event wifi
    EMBED_TXTFILES
        "${project_dir}/app/main.html"
        "${project_dir}/app/saved.html"
    EMBED_FILES
        "${project_dir}/app/router.ico"
)
