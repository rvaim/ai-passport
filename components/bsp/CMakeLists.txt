idf_component_register(
    SRCS "src/bsp_i2c.c"
         "src/bsp_display.c"
         "src/bsp_display_lvgl.c"
         "src/bsp_button.c"
         "src/bsp_audio.c"
         "src/bsp_battery.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_lcd
    PRIV_REQUIRES esp_driver_i2s esp_lvgl_port button esp_adc esp_codec_dev
)
