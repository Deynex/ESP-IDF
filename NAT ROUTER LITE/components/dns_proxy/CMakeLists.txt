idf_component_register(
    SRCS "dns_proxy.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES lwip esp_hw_support
)
