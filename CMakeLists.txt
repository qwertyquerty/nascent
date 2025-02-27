cmake_minimum_required(VERSION 3.22)
cmake_policy(VERSION 3.22)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")

project(Nascent CXX)

find_library(GDIP       gdiplus)
find_library(GDI32      gdi32)
find_library(OpenGL32   opengl32)
find_library(dwmapi     dwmapi)
find_library(user32     user32)
find_library(Shlwapi    shlwapi)

find_package(Boost COMPONENTS filesystem REQUIRED)

add_subdirectory("src/libs")
add_subdirectory("src/main")
add_subdirectory("src/core")
add_subdirectory("src/gse")
add_subdirectory("src/game")

set_target_properties(NascentMain PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG     ${CMAKE_BINARY_DIR})
set_target_properties(NascentMain PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE   ${CMAKE_BINARY_DIR})
