cmake_minimum_required(VERSION 3.22)

project(nascent_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

function(nascent_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /wd4244 /wd4267 /wd4458)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

get_filename_component(NASCENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_subdirectory(${NASCENT_ROOT}/src/libs/json ${CMAKE_CURRENT_BINARY_DIR}/json)
add_subdirectory(${NASCENT_ROOT}/src/libs/picosha2 ${CMAKE_CURRENT_BINARY_DIR}/picosha2)
add_subdirectory(${NASCENT_ROOT}/src/core ${CMAKE_CURRENT_BINARY_DIR}/core)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(nascent_core MODULE src/module.cpp)
target_link_libraries(nascent_core PRIVATE core)
nascent_target_warnings(nascent_core)

install(TARGETS nascent_core LIBRARY DESTINATION .)
install(FILES nascent_core.pyi DESTINATION .)
