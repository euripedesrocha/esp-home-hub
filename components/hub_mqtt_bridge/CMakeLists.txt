idf_component_register(SRCS "hub_mqtt_bridge.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES hub_events
                    PRIV_REQUIRES log)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
