add_executable(PipelinedCompute main.cpp)

target_link_libraries(PipelinedCompute PRIVATE PixelKiln)

pixelkiln_add_shaders(PipelinedCompute shaders/accumulate.comp)
