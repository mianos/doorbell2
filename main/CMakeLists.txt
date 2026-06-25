set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

idf_component_register(SRCS "main.cpp" "DoorbellWebServer.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES
                         nvsstoragemanager
                         wifimanager
                         appsettings
                         audioplayer
                         mqttwrapper
                         webserver
                         radar
                         jsonwrapper
                         espressif__mqtt
                         esp_netif
                         esp_event
                         nvs_flash
                         app_update)
