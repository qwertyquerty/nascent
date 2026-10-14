set(NASCENT_PACKAGE_ARCH "" CACHE STRING "Architecture name used in the package file names, detected when empty")
set(NASCENT_APPIMAGETOOL "" CACHE FILEPATH "appimagetool used to build the Linux AppImage")

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

string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" NASCENT_NUMERIC_VERSION "${NASCENT_VERSION}")

if(NOT NASCENT_NUMERIC_VERSION)
    set(NASCENT_NUMERIC_VERSION "0.0.0")
endif()

set(nascent_package_name "nascent-${NASCENT_VERSION}-${nascent_os}-${nascent_arch}")
set(nascent_stage "${CMAKE_BINARY_DIR}/portable")
set(nascent_root "${nascent_stage}/${nascent_package_name}")
set(nascent_dist "${CMAKE_BINARY_DIR}/dist")
set(nascent_libs "${PROJECT_SOURCE_DIR}/src/libs")

if(APPLE)
    set(nascent_contents "${nascent_root}/Nascent.app/Contents")
    set(nascent_binary_dir "${nascent_contents}/MacOS")
    set(nascent_assets_dir "${nascent_contents}/Resources")
else()
    set(nascent_binary_dir "${nascent_root}")
    set(nascent_assets_dir "${nascent_root}")
endif()

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

set(nascent_platform_commands)

if(APPLE)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in" "${CMAKE_BINARY_DIR}/packaging/Info.plist" @ONLY)
    list(APPEND nascent_platform_commands
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/packaging/Info.plist" "${nascent_contents}/Info.plist"
        COMMAND ${CMAKE_COMMAND} "-DNASCENT_ICON_SOURCE=${PROJECT_SOURCE_DIR}/assets/logo/logo_n.png" "-DNASCENT_ICON_OUTPUT_DIR=${nascent_assets_dir}" -P "${PROJECT_SOURCE_DIR}/packaging/macos/MakeIcon.cmake"
        COMMAND codesign --force --deep --sign - "${nascent_root}/Nascent.app"
    )
endif()

add_custom_target(package_stage
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${nascent_root}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_binary_dir}" "${nascent_assets_dir}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:NascentMain>" "${nascent_binary_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/README.md" "${nascent_root}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/skins" "${nascent_assets_dir}/assets/skins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/fonts" "${nascent_assets_dir}/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/logo" "${nascent_assets_dir}/assets/logo"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${nascent_libs}/kissfft/LICENSES" "${nascent_root}/licenses/kissfft"
    ${nascent_copy_commands}
    ${nascent_platform_commands}
    COMMENT "Staging ${nascent_root}"
    VERBATIM
)
add_dependencies(package_stage NascentMain)

add_custom_target(portable_zip
    COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_dist}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${nascent_dist}/${nascent_package_name}.zip"
    COMMAND ${CMAKE_COMMAND} -E chdir "${nascent_stage}" ${CMAKE_COMMAND} -E tar cf "${nascent_dist}/${nascent_package_name}.zip" --format=zip "${nascent_package_name}"
    COMMENT "Packaging ${nascent_dist}/${nascent_package_name}.zip"
    VERBATIM
)
add_dependencies(portable_zip package_stage)

if(WIN32)
    find_program(NASCENT_ISCC NAMES ISCC iscc PATHS "C:/Program Files (x86)/Inno Setup 6" "C:/Program Files/Inno Setup 6")
    file(TO_NATIVE_PATH "${nascent_root}" nascent_native_root)
    file(TO_NATIVE_PATH "${nascent_dist}" nascent_native_dist)
    file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/src/main/nascent.ico" nascent_native_icon)

    if(NASCENT_ISCC)
        set(nascent_installer_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_dist}"
            COMMAND "${NASCENT_ISCC}" /Q
                "/DAppVersion=${NASCENT_VERSION}"
                "/DNumericVersion=${NASCENT_NUMERIC_VERSION}.0"
                "/DSourceDir=${nascent_native_root}"
                "/DOutputDir=${nascent_native_dist}"
                "/DOutputBaseFilename=${nascent_package_name}-setup"
                "/DIconFile=${nascent_native_icon}"
                "${PROJECT_SOURCE_DIR}/packaging/windows/nascent.iss"
        )
        set(nascent_installer_comment "Packaging ${nascent_dist}/${nascent_package_name}-setup.exe")
    endif()
elseif(APPLE)
    set(nascent_dmg_root "${CMAKE_BINARY_DIR}/dmg")
    set(nascent_installer_commands
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${nascent_dmg_root}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_dmg_root}" "${nascent_dist}"
        COMMAND ditto "${nascent_root}/Nascent.app" "${nascent_dmg_root}/Nascent.app"
        COMMAND ${CMAKE_COMMAND} -E create_symlink /Applications "${nascent_dmg_root}/Applications"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${nascent_dist}/${nascent_package_name}.dmg"
        COMMAND hdiutil create -volname Nascent -srcfolder "${nascent_dmg_root}" -ov -format UDZO "${nascent_dist}/${nascent_package_name}.dmg"
    )
    set(nascent_installer_comment "Packaging ${nascent_dist}/${nascent_package_name}.dmg")
elseif(NASCENT_APPIMAGETOOL)
    set(nascent_appdir "${CMAKE_BINARY_DIR}/AppDir")

    if(nascent_arch STREQUAL "arm64")
        set(nascent_appimage_arch "aarch64")
    else()
        set(nascent_appimage_arch "${nascent_arch}")
    endif()

    set(nascent_installer_commands
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${nascent_appdir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${nascent_appdir}/usr/bin" "${nascent_dist}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${nascent_root}" "${nascent_appdir}/usr/bin"
        COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/packaging/linux/AppRun" "${PROJECT_SOURCE_DIR}/packaging/linux/nascent.desktop" "${nascent_appdir}"
        COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/assets/logo/logo_n.png" "${nascent_appdir}/nascent.png"
        COMMAND chmod 755 "${nascent_appdir}/AppRun" "${nascent_appdir}/usr/bin/Nascent"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${nascent_dist}/${nascent_package_name}.AppImage"
        COMMAND ${CMAKE_COMMAND} -E env "ARCH=${nascent_appimage_arch}" APPIMAGE_EXTRACT_AND_RUN=1 "${NASCENT_APPIMAGETOOL}" "${nascent_appdir}" "${nascent_dist}/${nascent_package_name}.AppImage"
    )
    set(nascent_installer_comment "Packaging ${nascent_dist}/${nascent_package_name}.AppImage")
endif()

if(nascent_installer_commands)
    add_custom_target(installer ${nascent_installer_commands} COMMENT "${nascent_installer_comment}" VERBATIM)
else()
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E echo "The installer needs Inno Setup on Windows or NASCENT_APPIMAGETOOL on Linux"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
endif()

add_dependencies(installer package_stage)
