add_executable(Window main.cpp)

target_link_libraries(Window PRIVATE PixelKiln glfw)

pixelkiln_add_shaders(Window shaders/spin.vert shaders/spin.frag)
