message(STATUS "Building Core Module")

add_library(core
    src/Accuracy.cpp
    src/Attempt.cpp
    src/Chart.cpp
    src/ChartSearch.cpp
    src/ChartSource.cpp
    src/DifficultyCache.cpp
    src/DifficultyBasis.cpp
    src/DifficultyCalculator.cpp
    src/DifficultyEvents.cpp
    src/Hit.cpp
    src/OsuFormat.cpp
    src/PlaySession.cpp
    src/ReleaseUpdate.cpp
    src/Replay.cpp
    src/Results.cpp
    src/SettingsStore.cpp
    src/Sha256.cpp
    src/Mp3Padding.cpp
    src/SongClock.cpp
    src/StepManiaFormat.cpp
)

set(NASCENT_DIFFICULTY_SOURCES
    include/Accuracy.h
    include/Chart.h
    include/Const.h
    include/DifficultyBasis.h
    include/DifficultyCalculator.h
    include/DifficultyEvents.h
    include/DifficultyModel.h
    include/DifficultySkillModel.h
    src/Chart.cpp
    src/DifficultyBasis.cpp
    src/DifficultyCalculator.cpp
    src/DifficultyEvents.cpp
)
set(NASCENT_DIFFICULTY_SOURCE_DIGESTS "")

foreach(source ${NASCENT_DIFFICULTY_SOURCES})
    file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/${source} digest)
    string(APPEND NASCENT_DIFFICULTY_SOURCE_DIGESTS "${source}=${digest};")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source})
endforeach()

string(SHA256 NASCENT_DIFFICULTY_SOURCE_HASH "${NASCENT_DIFFICULTY_SOURCE_DIGESTS}")
set_source_files_properties(src/DifficultyCache.cpp PROPERTIES COMPILE_DEFINITIONS "NASCENT_DIFFICULTY_SOURCE_HASH=\"${NASCENT_DIFFICULTY_SOURCE_HASH}\"")

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(core PUBLIC json PRIVATE picosha2)
nascent_target_warnings(core)
