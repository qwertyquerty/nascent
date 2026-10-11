set(NASCENT_PACKAGE_ARCH "" CACHE STRING "Architecture name used in the zip file name, detected when empty")

if(WIN32)
    set(nascent_os "win")
elseif(APPLE)
    set(nascent_os "mac")
else()
    set(nascent_os "linux")
endif()

if(NASCENT_PACKAGE_ARCH)
    set(nascent_arch "${NASCENT_PACKAGE_ARCH}")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES nascent_arch_count)

    if(nascent_arch_count GREATER 1)
        set(nascent_arch "universal")
    else()
        set(nascent_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" nascent_arch)
endif()

if(nascent_arch MATCHES "^(amd64|x86_64|x64)$")
    set(nascent_arch "x86_64")
elseif(nascent_arch MATCHES "^(aarch64|arm64)$")
    set(nascent_arch "arm64")
endif()

set(nascent_zip_name "nascent-${NASCENT_VERSION}-${nascent_os}-${nascent_arch}")
set(nascent_stage "${CMAKE_BINARY_DIR}/portable")
set(nascent_root "${nascent_stage}/${nascent_zip_name}")
set(nascent_libs "${PROJECT_SOURCE_DIR}/src/libs")

set(nascent_license_files
    "${nascent_libs}/licenses/olcPGEX_MiniAudio.txt|."
    "${nascent_libs}/licenses/miniaudio.txt|."
    "${nascent_libs}/licenses/nlohmann_json.txt|."
    "${nascent_libs}/licenses/open-arrow.txt|."
    "${nascent_libs}/freetype/LICENSE.TXT|freetype"
    "${nascent_libs}/freetype/docs/FTL.TXT|freetype"
    "${nascent_libs}/kissfft/COPYING|kissfft"
    "${nascent_libs}/discord-presence/LICENSE.md|discord-presence"
    "${nascent_libs}/stb/LICENSE|stb"
    "${nascent_libs}/picosha2/LICENSE|picosha2"
    "${nascent_libs}/olcPixelGameEngine/LICENCE.md|olcPixelGameEngine"
)

foreach(nascent_cpm_package fmt glaze miniz)
    if(EXISTS "${CPM_PACKAGE_${nascent_cpm_package}_SOURCE_DIR}/LICENSE")
        list(APPEND nascent_license_files "${CPM_PACKAGE_${nascent_cpm_package}_SOURCE_DIR}/LICENSE|${nascent_cpm_package}")
    endif()
endforeach()

if(EXISTS "${CPM_PACKAGE_curl_SOURCE_DIR}/COPYING")
    list(APPEND nascent_license_files "${CPM_PACKAGE_curl_SOURCE_DIR}/COPYING|curl")
endif()

set(nascent_copy_commands)

foreach(nascent_entry IN LISTS nascent_license_files)
    string(REPLACE "|" ";" nascent_entry "${nascent_entry}")
    list(GET nascent_entry 0 nascent_file)
    list(GET nascent_entry 1 nascent_folder)
    list(APPEND nascent_copy_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_root}/licenses/${nascent_folder}"
        COMMAND ${CMAKE_COMMAND} -E copy "${nascent_file}" "${nascent_root}/licenses/${nascent_folder}"
    )
endforeach()

add_custom_target(portable_zip
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${nascent_root}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_root}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:NascentMain>" "${PROJECT_SOURCE_DIR}/README.md" "${nascent_root}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/skins" "${nascent_root}/assets/skins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/fonts" "${nascent_root}/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${nascent_libs}/kissfft/LICENSES" "${nascent_root}/licenses/kissfft"
    ${nascent_copy_commands}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E chdir "${nascent_stage}" ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/dist/${nascent_zip_name}.zip" --format=zip "${nascent_zip_name}"
    DEPENDS NascentMain
    COMMENT "Packaging ${CMAKE_BINARY_DIR}/dist/${nascent_zip_name}.zip"
    VERBATIM
)
