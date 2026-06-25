idf_component_register(SRCS "src/AudioPlayer.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES
                         esp_driver_gpio
                         esp_driver_i2s
                         esp_http_client
                         chmorgan__esp-libhelix-mp3)
