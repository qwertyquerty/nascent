set(KISSFFT_TOOLS OFF CACHE BOOL "")
set(KISSFFT_TEST OFF CACHE BOOL "")
set(KISSFFT_STATIC ON CACHE BOOL "")
set(KISSFFT_OPENMP OFF CACHE BOOL "")
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "")

add_subdirectory(kissfft)
add_subdirectory(freetype)
add_subdirectory(olcPixelGameEngine)
add_subdirectory(miniaudio)
add_subdirectory(json)
add_subdirectory(discord-presence)
