message(STATUS "Building Game Module")

add_library(game
    src/EntityFFT.cpp
    src/EntityField.cpp
    src/GameNascent.cpp
    src/InputManager.cpp
    src/SceneCalibration.cpp
    src/SceneList.cpp
    src/ScenePlay.cpp
    src/SceneResults.cpp
    src/SceneTitle.cpp
    src/SettingsManager.cpp
    src/Skin.cpp
    src/Util.cpp
)

target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(game PUBLIC core gse olcPixelGameEngine kissfft discord-rpc)
nascent_target_warnings(game)
