idf_component_register(
    SRCS "src/passport_crc32.c"
         "src/passport_identity.c"
         "src/passport_storage.c"
         "src/passport_package.c"
         "src/passport_app_registry.c"
         "src/passport_theme.c"
         "src/passport_settings_model.c"
         "src/passport_settings.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp esp_hw_support fatfs wear_levelling json nvs_flash
)
