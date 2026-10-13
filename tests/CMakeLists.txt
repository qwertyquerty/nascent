add_executable(core_tests
    test_audio.cpp
    test_charts.cpp
    test_difficulty.cpp
    test_judging.cpp
    test_main.cpp
    test_replays.cpp
    test_results.cpp
    test_search.cpp
    test_settings.cpp
)
target_link_libraries(core_tests PRIVATE core)
nascent_target_warnings(core_tests)
nascent_target_runtime(core_tests)

add_test(NAME core_tests COMMAND core_tests)
