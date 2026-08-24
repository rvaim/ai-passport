idf_component_register(
    SRCS "src/plugin_format.c"
         "src/plugin_theme.c"
         "src/plugin_vm.c"
         "src/plugin_crypto.c"
         "src/plugin_store.c"
         "src/plugin_installer.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private"
    REQUIRES esp_partition mbedtls freertos
)
