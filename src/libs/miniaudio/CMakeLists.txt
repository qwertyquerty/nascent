message(STATUS "Building miniaudio")
add_library(miniaudio miniaudio.c stb_vorbis.c)

target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.)
