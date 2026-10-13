idf_component_register(SRCS "hub_web.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES hub_events hub_system esp_common
                    PRIV_REQUIRES esp_http_server esp_timer)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
