set(KISSFFT_TOOLS OFF CACHE BOOL "")
set(KISSFFT_TEST OFF CACHE BOOL "")
set(KISSFFT_STATIC ON CACHE BOOL "")
set(KISSFFT_OPENMP OFF CACHE BOOL "")
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "")

set(FT_DISABLE_ZLIB ON CACHE BOOL "")
set(FT_DISABLE_BZIP2 ON CACHE BOOL "")
set(FT_DISABLE_PNG ON CACHE BOOL "")
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "")
set(FT_DISABLE_BROTLI ON CACHE BOOL "")

set(BUILD_SHARED_LIBS OFF)

find_package(Threads REQUIRED)

add_subdirectory(kissfft)
add_subdirectory(freetype)
add_subdirectory(miniaudio)
add_subdirectory(json)
add_subdirectory(picosha2)

include(discord-presence/cmake/CPM.cmake)
CPMAddPackage("gh:fmtlib/fmt#11.2.0")
set(DISCORD_RPC_NO_GLAZE ON)
add_subdirectory(discord-presence)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_subdirectory(olc)
