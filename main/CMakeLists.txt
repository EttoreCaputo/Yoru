idf_component_register(SRCS "main.c"
                            "board_i2c.c"
                            "board_display.c"
                            "board_touch.c"
                            "board_audio.c"
                            "lvgl_port.c"
                            "eyes.c"
                            "behaviour.c"
                       INCLUDE_DIRS "."
                       LDFRAGMENTS "linker.lf"
                       REQUIRES esp_app_format esp_codec_dev esp_driver_gpio esp_driver_i2c esp_driver_i2s
                                esp_driver_spi esp_lcd esp_lcd_co5300 esp_lcd_touch_cst9217 esp_timer lvgl)
