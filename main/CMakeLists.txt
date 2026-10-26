set(platformio_component "")
if(EXISTS "$ENV{IDF_PATH}/components/__pio_env/CMakeLists.txt")
    set(platformio_component __pio_env)
endif()
idf_component_register(SRCS "receiver.c" "radio.c" "oled.c" "adsb.c" "web.c" "gnss.c" "wifi_manager.c" "tcp_stream.c" "trail_cache.c"
    INCLUDE_DIRS "." "../vendor/lr2021-sdr/include"
    REQUIRES esp_driver_uart esp_driver_gpio esp_driver_spi esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_http_server esp_wifi esp_netif esp_event nvs_flash json
    PRIV_REQUIRES ${platformio_component}
    EMBED_FILES "ui.html" "ui.js" "live.js" "style.css" "world.json.gz" "setup.html" "setup.js")
