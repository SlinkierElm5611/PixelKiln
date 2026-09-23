add_executable(TorusKnot main.cpp)

target_link_libraries(TorusKnot PRIVATE PixelKiln glfw)
target_include_directories(TorusKnot PRIVATE ../common)

pixelkiln_add_shaders(TorusKnot
    shaders/knot.vert
    shaders/knot.frag)
