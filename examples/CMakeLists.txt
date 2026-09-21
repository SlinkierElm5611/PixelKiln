# Compiles GLSL shaders into SPIR-V C arrays (glslc -mfmt=c) that can be #included as an array initializer:
#   static const uint32_t spirv[] =
#   #include "name.comp.h"
#   ;
function(pixelkiln_add_shaders target)
    set(outputDir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${outputDir})
    foreach(shader ${ARGN})
        get_filename_component(name ${shader} NAME)
        set(output ${outputDir}/${name}.h)
        add_custom_command(
            OUTPUT ${output}
            COMMAND Vulkan::glslc -mfmt=c -o ${output} ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
            VERBATIM)
        target_sources(${target} PRIVATE ${output})
    endforeach()
    target_include_directories(${target} PRIVATE ${outputDir})
endfunction()

add_subdirectory(HeadlessRender)
add_subdirectory(ComputeBasic)
add_subdirectory(PipelinedCompute)
