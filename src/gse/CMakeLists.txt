message(STATUS "Building GSE Module")

add_library(gse
    src/FrameClock.cpp
    src/Game.cpp
    src/Overlay.cpp
    src/Scene.cpp
    src/SceneStack.cpp
    src/Transition.cpp
    src/audio/AudioMixer.cpp
    src/fx/ParticleEmitter.cpp
    src/input/Input.cpp
    src/input/KeyEvents.cpp
    src/input/TextInput.cpp
    src/platform/Platform.cpp
    src/render/MipDecal.cpp
    src/render/ShapeSprite.cpp
    src/render/SpriteDecal.cpp
    src/render/TextCache.cpp
    src/ui/Layout.cpp
    src/ui/ProgressBar.cpp
    src/ui/RowField.cpp
    src/ui/TextField.cpp
    src/ui/UiForm.cpp
)

target_include_directories(gse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gse PUBLIC olcPixelGameEngine)
nascent_target_warnings(gse)

if(WIN32)
    target_sources(gse PRIVATE src/platform/windows/PlatformWindows.cpp src/platform/windows/WindowHook.cpp)
    target_link_libraries(gse PRIVATE shell32 ole32)
    target_compile_definitions(gse PRIVATE _WIN32_WINNT=0x0601)
elseif(APPLE)
    target_sources(gse PRIVATE src/platform/posix/PlatformPosix.cpp src/platform/posix/WindowHook.cpp src/platform/mac/PlatformMac.cpp src/platform/mac/KeySourceMac.mm)
    set_source_files_properties(src/platform/mac/KeySourceMac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(gse PRIVATE "-framework AppKit")
else()
    target_sources(gse PRIVATE src/platform/posix/PlatformPosix.cpp src/platform/posix/WindowHook.cpp src/platform/linux/PlatformLinux.cpp src/platform/linux/KeySourceX11.cpp)
endif()
