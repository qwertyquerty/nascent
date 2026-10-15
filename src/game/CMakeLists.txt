message(STATUS "Building Game Module")

add_library(game
    src/Account.cpp
    src/BeatmapDownloads.cpp
    src/ChartLibrary.cpp
    src/ChartMenu.cpp
    src/EntityFFT.cpp
    src/EntityField.cpp
    src/EtternaSkin.cpp
    src/EntityStarfield.cpp
    src/GameNascent.cpp
    src/InputManager.cpp
    src/KeyBindField.cpp
    src/LeaderboardPanel.cpp
    src/Favorites.cpp
    src/LocalBests.cpp
    src/LocalReplays.cpp
    src/PauseOverlay.cpp
    src/QuitOverlay.cpp
    src/ReplayMenu.cpp
    src/RichPresence.cpp
    src/SceneAccount.cpp
    src/SceneBrowser.cpp
    src/SceneCalibration.cpp
    src/SceneInput.cpp
    src/SceneList.cpp
    src/SceneOptions.cpp
    src/ScenePlay.cpp
    src/SceneResults.cpp
    src/SceneTitle.cpp
    src/ServerApi.cpp
    src/ServerClient.cpp
    src/SettingsManager.cpp
    src/Skin.cpp
    src/StatusBar.cpp
    src/SubmissionQueue.cpp
    src/ToastOverlay.cpp
    src/TimingBar.cpp
    src/Updater.cpp
    src/Util.cpp
)

target_include_directories(game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(game PRIVATE NASCENT_VERSION_STRING="${NASCENT_VERSION}")
target_link_libraries(game PUBLIC core gse olcPixelGameEngine kissfft discord-rpc PRIVATE CURL::libcurl miniz stb)
nascent_target_warnings(game)
