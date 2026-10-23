add_executable(core_tests
    test_audio.cpp
    test_charts.cpp
    test_difficulty.cpp
    test_gse.cpp
    test_judging.cpp
    test_library_cache.cpp
    test_main.cpp
    test_replays.cpp
    test_results.cpp
    test_search.cpp
    test_settings.cpp
    test_stepmania.cpp
    test_updates.cpp
    ${CMAKE_SOURCE_DIR}/src/game/src/LibraryCache.cpp
)
target_include_directories(core_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/game/include)
target_link_libraries(core_tests PRIVATE core gse)
nascent_target_warnings(core_tests)
nascent_target_runtime(core_tests)

add_test(NAME core_tests COMMAND core_tests)
