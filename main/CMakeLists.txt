idf_component_register(SRCS "app.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash esp_netif esp_event freertos hub_connection hub_system)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
