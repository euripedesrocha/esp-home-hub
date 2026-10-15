idf_component_register(SRCS "app.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash esp_netif esp_event freertos
                    hub_connection hub_error hub_events hub_mdns hub_mqtt hub_mqtt_bridge hub_system
                    hub_thread hub_thread_bridge hub_web)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
