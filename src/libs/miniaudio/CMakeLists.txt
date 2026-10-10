message(STATUS "Building miniaudio")
add_library(miniaudio miniaudio.c stb_vorbis.c)

target_include_directories(miniaudio SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.)
target_link_libraries(miniaudio PUBLIC Threads::Threads)

if(APPLE)
    target_link_libraries(miniaudio PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(miniaudio PUBLIC ${CMAKE_DL_LIBS} m)
endif()
