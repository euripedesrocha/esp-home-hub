idf_component_register(SRCS "thread_client.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES console esp_event esp_netif freertos nvs_flash openthread vfs)

include(${CMAKE_SOURCE_DIR}/../../cmake/project_checks.cmake)
