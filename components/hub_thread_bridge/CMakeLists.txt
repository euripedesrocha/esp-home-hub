idf_component_register(SRCS "hub_thread_bridge.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES hub_events
                    PRIV_REQUIRES hub_thread log openthread)

include(${CMAKE_SOURCE_DIR}/cmake/project_checks.cmake)
