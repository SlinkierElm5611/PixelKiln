add_executable(ReactionDiffusion main.cpp)

target_link_libraries(ReactionDiffusion PRIVATE PixelKiln glfw)
target_include_directories(ReactionDiffusion PRIVATE ../common)

pixelkiln_add_shaders(ReactionDiffusion
    shaders/step.comp
    shaders/colorize.comp
    ../common/shaders/fullscreen.vert
    ../common/shaders/blit.frag)
