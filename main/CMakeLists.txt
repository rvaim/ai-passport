idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bsp passport_core passport_ui passport_link passport_runtime nvs_flash
)
