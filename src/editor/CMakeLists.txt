message(STATUS "Building Chart Editor")

CPMAddPackage(
    NAME imgui
    GITHUB_REPOSITORY ocornut/imgui
    GIT_TAG v1.92.9b-docking
    DOWNLOAD_ONLY YES
)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)

target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC olcPixelGameEngine)

add_executable(NascentEditor
    src/ArchiveExport.cpp
    src/ChartDocument.cpp
    src/EditorField.cpp
    src/EditorGame.cpp
    src/EditorMain.cpp
    src/FileDialog.cpp
    src/ImGuiBackend.cpp
    src/OsuExport.cpp
    src/PatternGenerator.cpp
    src/Playback.cpp
    src/StepManiaExport.cpp
    src/Waveform.cpp
)

target_include_directories(NascentEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(NascentEditor PRIVATE game imgui miniz)
nascent_target_warnings(NascentEditor)
nascent_target_runtime(NascentEditor)

if(MSVC)
    target_sources(NascentEditor PRIVATE ${PROJECT_SOURCE_DIR}/src/main/nascent.manifest nascent_editor_icon.rc)
elseif(MINGW)
    target_sources(NascentEditor PRIVATE ${PROJECT_SOURCE_DIR}/src/main/nascent.rc nascent_editor_icon.rc)
endif()

if(WIN32)
    set_target_properties(NascentEditor PROPERTIES WIN32_EXECUTABLE ON)

    if(MSVC)
        target_link_options(NascentEditor PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

set_target_properties(NascentEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${NASCENT_OUTPUT_DIRECTORY}/$<LOWER_CASE:$<CONFIG>>
)

if(NASCENT_BUILD_TESTS)
    add_executable(editor_tests
        tests/test_editor.cpp
        ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
        src/ArchiveExport.cpp
        src/ChartDocument.cpp
        src/OsuExport.cpp
        src/PatternGenerator.cpp
        src/StepManiaExport.cpp
    )

    target_include_directories(editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(editor_tests PRIVATE core miniz)
    nascent_target_warnings(editor_tests)
    nascent_target_runtime(editor_tests)
    add_test(NAME editor_tests COMMAND editor_tests)
endif()
