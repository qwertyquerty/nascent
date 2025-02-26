FILE(GLOB_RECURSE CORE_SOURCES "src/*")
set(CORE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include)

message(STATUS "Building Core Module")

add_library(core ${CORE_SOURCES})
target_include_directories(core PUBLIC ${CORE_HEADERS})
target_link_libraries(core PUBLIC json ${Boost_LIBRARIES})
