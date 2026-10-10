add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE core)
nascent_target_warnings(core_tests)
nascent_target_runtime(core_tests)

add_test(NAME core_tests COMMAND core_tests)
