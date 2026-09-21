add_executable(ComputeBasic main.cpp)

target_link_libraries(ComputeBasic PRIVATE PixelKiln)

pixelkiln_add_shaders(ComputeBasic shaders/scale.comp shaders/gradient.comp)
