cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
endif()

project(Nascent CXX C)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NASCENT_VERSION "0.0.0-dev" CACHE STRING "Version string used for packaging")
set(NASCENT_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin" CACHE PATH "Directory the game executable is written to, inside a per configuration subdirectory")
option(NASCENT_BUILD_TESTS "Build the Nascent unit tests" ON)
option(NASCENT_STATIC_RUNTIME "Link the C/C++ runtime statically where the toolchain allows it" ON)

get_property(NASCENT_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT NASCENT_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(MSVC AND NASCENT_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/permissive-> $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
endif()

function(nascent_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /wd4244 /wd4267 /wd4458)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

function(nascent_target_runtime target)
    if(NOT NASCENT_STATIC_RUNTIME OR MSVC OR APPLE)
        return()
    endif()

    if(MINGW)
        target_link_options(${target} PRIVATE -static)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
    endif()
endfunction()

add_subdirectory(src/libs)
add_subdirectory(src/core)
add_subdirectory(src/gse)
add_subdirectory(src/game)
add_subdirectory(src/main)

if(NASCENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
