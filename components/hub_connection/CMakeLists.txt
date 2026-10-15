idf_component_register(SRCS "hub_connection.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_netif esp_event hub_error
                    PRIV_REQUIRES esp_eth esp_wifi)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
