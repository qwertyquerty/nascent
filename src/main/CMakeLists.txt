add_executable(NascentMain nascent_main.cpp)
target_link_options(NascentMain PRIVATE -static-libgcc -static-libstdc++)
target_link_libraries(NascentMain core game olcPixelGameEngine gdiplus gdi32 opengl32 dwmapi user32 shlwapi)
