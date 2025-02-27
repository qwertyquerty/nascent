FILE(GLOB_RECURSE GAME_SOURCES "src/*")
set(GAME_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include)

message(STATUS "Building Game Module")

add_library(game ${GAME_SOURCES})
target_include_directories(game PUBLIC ${GAME_HEADERS})
target_link_libraries(game PUBLIC core gse olcPixelGameEngine ${Boost_LIBRARIES})
