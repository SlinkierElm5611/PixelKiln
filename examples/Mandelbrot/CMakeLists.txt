add_executable(Mandelbrot main.cpp)

target_link_libraries(Mandelbrot PRIVATE PixelKiln glfw)
target_include_directories(Mandelbrot PRIVATE ../common)

pixelkiln_add_shaders(Mandelbrot
    shaders/mandelbrot.comp
    ../common/shaders/fullscreen.vert
    ../common/shaders/blit.frag)
