idf_component_register(
    SRCS "src/passport_crc32.c"
         "src/passport_navigation.c"
         "src/passport_identity.c"
         "src/passport_storage.c"
         "src/passport_app_storage_model.c"
         "src/passport_app_storage.c"
         "src/passport_text.c"
         "src/passport_manifest.c"
         "src/passport_package.c"
         "src/passport_app_registry.c"
         "src/passport_theme_parser.c"
         "src/passport_theme_resolver.c"
         "src/passport_theme.c"
         "src/passport_settings_model.c"
         "src/passport_settings.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bsp esp_hw_support esp_partition fatfs wear_levelling json nvs_flash
)
