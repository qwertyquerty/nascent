message(STATUS "Building olcPixelGameEngine")
add_library(olcPixelGameEngine olcPixelGameEngine.h olcPGEX_MiniAudio.h olcPGEX_TTF.h)

target_link_libraries(olcPixelGameEngine miniaudio freetype)

set_target_properties(olcPixelGameEngine PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(olcPixelGameEngine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/.)
