message(STATUS "Building json")
add_library(json INTERFACE json.h)

target_include_directories(json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
