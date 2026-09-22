add_executable(Particles main.cpp)

target_link_libraries(Particles PRIVATE PixelKiln glfw)
target_include_directories(Particles PRIVATE ../common)

pixelkiln_add_shaders(Particles
    shaders/simulate.comp
    shaders/points.vert
    shaders/points.frag
    shaders/fade.frag
    ../common/shaders/fullscreen.vert
    ../common/shaders/blit.frag)
