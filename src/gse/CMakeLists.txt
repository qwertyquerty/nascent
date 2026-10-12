message(STATUS "Building GSE Module")

add_library(gse
    src/Entity.cpp
    src/Game.cpp
    src/KeyEvents.cpp
    src/TextInput.cpp
    src/Platform.cpp
    src/Scene.cpp
    src/WindowHook.cpp
)

target_include_directories(gse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gse PUBLIC olcPixelGameEngine)
nascent_target_warnings(gse)

if(WIN32)
    target_link_libraries(gse PRIVATE shell32 ole32)
    target_compile_definitions(gse PRIVATE _WIN32_WINNT=0x0601)
elseif(APPLE)
    target_sources(gse PRIVATE src/KeySourceMac.mm)
    set_source_files_properties(src/KeySourceMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gse PRIVATE "-framework AppKit")
else()
    target_sources(gse PRIVATE src/KeySourceX11.cpp)
endif()
