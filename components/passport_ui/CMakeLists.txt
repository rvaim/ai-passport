idf_component_register(
    SRCS "src/passport_ui.c"
         "src/passport_ui_font_zh_14.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES bsp passport_core
)

if(NOT CONFIG_LV_USE_FONT_COMPRESSED)
    message(FATAL_ERROR
        "passport_ui_font_zh_14 uses LVGL RLE compression; "
        "CONFIG_LV_USE_FONT_COMPRESSED must be enabled"
    )
endif()
