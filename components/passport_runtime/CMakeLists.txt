idf_component_register(
    SRCS "src/passport_runtime_lua.c"
         "src/passport_runtime_storage.c"
         "src/passport_runtime_ui.c"
         "src/passport_runtime_json.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp passport_core passport_link
    PRIV_REQUIRES json lua esp_hw_support passport_ui
)

# The managed Lua core is built with this private definition. Every source
# using its C ABI must use the same integer/number widths.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUA_32BITS=1)
