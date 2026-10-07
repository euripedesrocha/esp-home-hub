idf_component_register(SRCS "hub_system.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES hub_events
                    PRIV_REQUIRES esp_event log)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
