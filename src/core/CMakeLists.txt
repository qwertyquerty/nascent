message(STATUS "Building Core Module")

add_library(core
    src/Accuracy.cpp
    src/Attempt.cpp
    src/Chart.cpp
    src/DifficultyCalculator.cpp
    src/Hit.cpp
    src/OsuFormat.cpp
    src/PlaySession.cpp
    src/Replay.cpp
    src/Results.cpp
    src/SettingsStore.cpp
    src/Sha256.cpp
    src/Mp3Padding.cpp
    src/SongClock.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(core PUBLIC json PRIVATE picosha2)
nascent_target_warnings(core)
