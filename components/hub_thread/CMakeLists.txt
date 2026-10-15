idf_component_register(SRCS "hub_thread.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES hub_error openthread
                    PRIV_REQUIRES esp_netif vfs)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
