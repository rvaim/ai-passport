idf_component_register(
    SRCS "src/passport_runtime_lua.c"
         "src/passport_runtime_json.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp passport_core passport_link
    PRIV_REQUIRES json lua esp_hw_support passport_ui
)
