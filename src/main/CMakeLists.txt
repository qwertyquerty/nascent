add_executable(NascentMain nascent_main.cpp)

if(MSVC)
    target_sources(NascentMain PRIVATE nascent.manifest)
elseif(MINGW)
    target_sources(NascentMain PRIVATE nascent.rc)
endif()
target_link_libraries(NascentMain PRIVATE game)
nascent_target_warnings(NascentMain)
nascent_target_runtime(NascentMain)

set_target_properties(NascentMain PROPERTIES
    OUTPUT_NAME Nascent
    RUNTIME_OUTPUT_DIRECTORY ${NASCENT_OUTPUT_DIRECTORY}/$<LOWER_CASE:$<CONFIG>>
)
