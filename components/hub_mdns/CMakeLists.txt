idf_component_register(SRCS "hub_mdns.cpp"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES mdns)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
