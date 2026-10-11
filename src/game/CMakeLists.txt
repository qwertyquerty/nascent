message(STATUS "Building Game Module")

add_library(game
    src/Account.cpp
    src/BeatmapDownloads.cpp
    src/EntityFFT.cpp
    src/EntityField.cpp
    src/EntityStarfield.cpp
    src/GameNascent.cpp
    src/InputManager.cpp
    src/LocalBests.cpp
    src/ProgressBar.cpp
    src/RowField.cpp
    src/SceneAccount.cpp
    src/SceneBrowser.cpp
    src/SceneCalibration.cpp
    src/SceneList.cpp
    src/ScenePlay.cpp
    src/SceneResults.cpp
    src/SceneTitle.cpp
    src/ServerApi.cpp
    src/ServerClient.cpp
    src/SettingsManager.cpp
    src/Skin.cpp
    src/StatusBar.cpp
    src/TextCache.cpp
    src/TextField.cpp
    src/UiForm.cpp
    src/Util.cpp
)

target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(game PUBLIC core gse olcPixelGameEngine kissfft discord-rpc PRIVATE CURL::libcurl miniz)
nascent_target_warnings(game)
