idf_component_register(SRCS "hub_mqtt.cpp"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES freertos mosquitto)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
