# PixelKiln test suite. Every TEST(name) in tests/test*.cpp and every example becomes its own ctest test.
#
#   ctest --test-dir <build> --output-on-failure
#   ctest --test-dir <build> -L example            # only the examples
#   ctest --test-dir <build> -L MoltenVK_icd       # only one driver variant (see PIXELKILN_TEST_DRIVER_FILES)
#
# Tests run under the Khronos validation layer with synchronization validation, and any validation message fails
# the test. The layer comes with the Vulkan SDK; without it the tests still run, just unvalidated.

option(PIXELKILN_TEST_VALIDATION "Run tests under the validation layer (with sync validation), failing on any message" ON)
set(PIXELKILN_TEST_DRIVER_FILES "" CACHE STRING
    "Vulkan ICD manifests (VK_DRIVER_FILES) to additionally run every test against, one variant per manifest")
set(PIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE "" CACHE FILEPATH
    "ICD manifest of a device with 4 queue families of 1 queue each (e.g. MoltenVK). Adds a variant where the profiles \
layer reports family 1 as transfer-only, like NVIDIA/AMD desktop GPUs, so validation checks the transfer queue against it")

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test*.cpp)
add_executable(PixelKilnTests ${TEST_SOURCES})
target_link_libraries(PixelKilnTests PRIVATE PixelKiln)
target_include_directories(PixelKilnTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
pixelkiln_add_shaders(PixelKilnTests
    shaders/scale.comp
    shaders/double.comp
    shaders/gradient.comp
    shaders/store.comp
    shaders/twoUniforms.comp
    shaders/triangle.comp
    shaders/readback.comp
    shaders/position.vert
    shaders/solid.frag
    shaders/twoTargets.frag
    shaders/textured.vert
    shaders/textured.frag
    shaders/background.vert
    shaders/instanced.vert)

# Test names are the TEST(name) declarations at the start of a line. Editing a test source re-runs configure.
set(TEST_NAMES)
foreach(source ${TEST_SOURCES})
    file(STRINGS ${source} declarations REGEX "^TEST\\([A-Za-z0-9_]+\\)")
    foreach(declaration ${declarations})
        string(REGEX REPLACE "^TEST\\(([A-Za-z0-9_]+)\\).*" "\\1" name "${declaration}")
        list(APPEND TEST_NAMES ${name})
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
endforeach()

if(WIN32)
    set(LAYER_SEPARATOR "\\;")
else()
    set(LAYER_SEPARATOR ":")
endif()
set(VALIDATION_ENVIRONMENT
    VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true
    VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true
    VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn
    VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG)

# Registers one ctest test. `environment` is the name of a list variable with extra environment entries,
# `extraLayer` is appended below the validation layer.
function(pixelkiln_register_test name labels environment extraLayer)
    add_test(NAME ${name} COMMAND ${ARGN})
    set(testEnvironment ${${environment}})
    set(layers "")
    if(PIXELKILN_TEST_VALIDATION)
        set(layers "VK_LAYER_KHRONOS_validation")
        list(APPEND testEnvironment ${VALIDATION_ENVIRONMENT})
        set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION
            "Validation Error|Validation Warning|SYNC-HAZARD|PixelKiln validation:")
    endif()
    if(extraLayer)
        if(layers)
            set(layers "${layers}${LAYER_SEPARATOR}${extraLayer}")
        else()
            set(layers "${extraLayer}")
        endif()
    endif()
    if(layers)
        list(APPEND testEnvironment "VK_INSTANCE_LAYERS=${layers}")
    endif()
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${testEnvironment}"
        LABELS "${labels}"
        TIMEOUT 300
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

# Registers a test once for the default driver and once per configured driver variant.
function(pixelkiln_add_test name label)
    set(noEnvironment "")
    pixelkiln_register_test(${name} "${label};default" noEnvironment "" ${ARGN})
    foreach(driver ${PIXELKILN_TEST_DRIVER_FILES})
        get_filename_component(driverName ${driver} NAME_WE)
        set(driverEnvironment "VK_DRIVER_FILES=${driver}")
        pixelkiln_register_test(${name}.${driverName} "${label};${driverName}" driverEnvironment "" ${ARGN})
    endforeach()
    if(PIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE)
        set(transferOnlyEnvironment
            "VK_DRIVER_FILES=${PIXELKILN_TEST_TRANSFER_ONLY_DRIVER_FILE}"
            "VK_KHRONOS_PROFILES_PROFILE_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/profiles"
            "VK_KHRONOS_PROFILES_PROFILE_NAME=VP_PIXELKILN_transfer_only"
            "VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES=SIMULATE_QUEUE_FAMILY_PROPERTIES_BIT"
            "VK_KHRONOS_PROFILES_EMULATE_PORTABILITY=false")
        pixelkiln_register_test(${name}.transferOnly "${label};transferOnly" transferOnlyEnvironment
                                VK_LAYER_KHRONOS_profiles ${ARGN})
    endif()
endfunction()

foreach(test ${TEST_NAMES})
    pixelkiln_add_test(${test} unit $<TARGET_FILE:PixelKilnTests> ${test})
endforeach()

foreach(example HeadlessRender ComputeBasic PipelinedCompute)
    pixelkiln_add_test(example.${example} example $<TARGET_FILE:${example}>)
endforeach()

# Fails if a TEST() wasn't picked up by the regex above (e.g. indented) or a name is registered twice.
add_test(NAME test_names_in_sync COMMAND $<TARGET_FILE:PixelKilnTests> --verify ${TEST_NAMES})
set_tests_properties(test_names_in_sync PROPERTIES LABELS "meta")
