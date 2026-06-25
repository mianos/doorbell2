idf_component_register(SRCS
                         "src/RadarSensor.cpp"
                         "src/Ld2450.cpp"
                         "src/Ld2450Decoder.cpp"
                         "src/LocalEP.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES
                         appsettings
                         jsonwrapper
                         esp_driver_uart
                         esp_driver_gpio
                         esp_timer)
