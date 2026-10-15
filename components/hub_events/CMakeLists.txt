idf_component_register(SRCS "hub_events.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event hub_error
                    PRIV_REQUIRES freertos)

include(${COMPONENT_DIR}/../../cmake/project_checks.cmake)
