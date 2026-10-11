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
CPMAddPackage(
    NAME miniz
    GITHUB_REPOSITORY richgel999/miniz
    GIT_TAG 3.1.0
    OPTIONS
        "BUILD_EXAMPLES OFF"
        "BUILD_FUZZERS OFF"
        "BUILD_TESTS OFF"
        "INSTALL_PROJECT OFF"
)
set(DISCORD_RPC_NO_GLAZE ON)
add_subdirectory(discord-presence)

if(WIN32)
    CPMAddPackage(
        NAME curl
        GITHUB_REPOSITORY curl/curl
        GIT_TAG curl-8_21_0
        OPTIONS
            "BUILD_CURL_EXE OFF"
            "BUILD_STATIC_LIBS ON"
            "BUILD_TESTING OFF"
            "BUILD_EXAMPLES OFF"
            "BUILD_LIBCURL_DOCS OFF"
            "BUILD_MISC_DOCS OFF"
            "ENABLE_CURL_MANUAL OFF"
            "CURL_DISABLE_INSTALL ON"
            "CURL_USE_SCHANNEL ON"
            "CURL_USE_OPENSSL OFF"
            "CURL_USE_LIBPSL OFF"
            "CURL_USE_LIBSSH2 OFF"
            "CURL_USE_LIBSSH OFF"
            "USE_LIBIDN2 OFF"
            "USE_WIN32_IDN OFF"
            "USE_NGHTTP2 OFF"
            "CURL_ZLIB OFF"
            "CURL_BROTLI OFF"
            "CURL_ZSTD OFF"
            "CURL_DISABLE_LDAP ON"
            "HTTP_ONLY ON"
    )
else()
    find_package(CURL REQUIRED)
    set_target_properties(CURL::libcurl PROPERTIES IMPORTED_GLOBAL TRUE)
endif()

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_subdirectory(olc)
