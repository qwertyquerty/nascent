message(STATUS "Building GSE Module")

add_library(gse
    src/Entity.cpp
    src/Game.cpp
    src/KeyEvents.cpp
    src/Platform.cpp
    src/Scene.cpp
    src/WindowHook.cpp
)

target_include_directories(gse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gse PUBLIC olcPixelGameEngine)
nascent_target_warnings(gse)

if(WIN32)
    target_link_libraries(gse PRIVATE shell32 ole32)
endif()
