add_executable(HeadlessRender main.cpp)

target_link_libraries(HeadlessRender PRIVATE PixelKiln)

pixelkiln_add_shaders(HeadlessRender shaders/quad.vert shaders/quad.frag shaders/background.vert shaders/background.frag)
