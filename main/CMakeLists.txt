idf_component_register(SRCS "ZigbeeGarageDoorOpener.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES driver nvs_flash espressif__esp-zigbee-lib)
