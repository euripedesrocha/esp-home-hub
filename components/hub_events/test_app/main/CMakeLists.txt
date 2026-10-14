idf_component_register(
    SRCS "test_main.cpp" "test_pubsub.cpp"
    REQUIRES hub_events
    PRIV_REQUIRES freertos
    WHOLE_ARCHIVE
)
