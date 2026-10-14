#include <catch2/catch_session.hpp>

#include <cstdio>

extern "C" void app_main() {
    int argc = 1;
    const char *argv[] = {"hub_events_tests", nullptr};
    const int result = Catch::Session().run(argc, argv);
    std::printf("hub_events_tests: %s\n", result == 0 ? "passed" : "failed");
}
