message(STATUS "Building miniaudio")
add_library(miniaudio INTERFACE miniaudio.h)

target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
