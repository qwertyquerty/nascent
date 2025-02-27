FILE(GLOB_RECURSE GSE_SOURCES "src/*")
set(GSE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include)

message(STATUS "Building GSE Module")

add_library(gse ${GSE_SOURCES})
target_include_directories(gse PUBLIC ${GSE_HEADERS})
target_link_libraries(gse PUBLIC olcPixelGameEngine ${Boost_LIBRARIES})
