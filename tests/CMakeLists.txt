# PixelKiln test suite. Every TEST(name) in tests/test*.cpp and every example becomes its own ctest test.
#
#   ctest --test-dir <build> --output-on-failure
#   ctest --test-dir <build> -L example            # only the examples
#   ctest --test-dir <build> -L MoltenVK_icd       # only one driver variant (see PIXELKILN_TEST_DRIVER_FILES)
#
# Tests run under the Khronos validation layer with synchronization validation. Validation messages are printed to
# help debugging, but a test's pass/fail comes from its own CHECK/CHECK_EQ/REQUIRE assertions (the process exit
# code), not from validation layer output: older layers have known false positives (the Vulkan SDK 1.3.290 one
# mistracks timeline semaphores, e.g. across queues, around presents, and waits between two signalled values) that
# don't reflect real bugs. The 1.4.357 layer reports nothing on this suite. The layer comes with the Vulkan SDK;
# without it the tests still run, just unvalidated.

option(PIXELKILN_TEST_VALIDATION "Run tests under the validation layer (with sync validation)" ON)
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
    shaders/instanced.vert
    shaders/msaaCoverage.comp
    shaders/uintValue.frag
    shaders/pushConstant.comp
    shaders/pushConstantColor.frag)

# Test names are the TEST(name) declarations at the start of a line. Editing a test source re-runs configure.
function(pixelkiln_scan_tests outVariable)
    set(names)
    foreach(source ${ARGN})
        file(STRINGS ${source} declarations REGEX "^TEST\\([A-Za-z0-9_]+\\)")
        foreach(declaration ${declarations})
            string(REGEX REPLACE "^TEST\\(([A-Za-z0-9_]+)\\).*" "\\1" name "${declaration}")
            list(APPEND names ${name})
        endforeach()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    endforeach()
    set(${outVariable} ${names} PARENT_SCOPE)
endfunction()
pixelkiln_scan_tests(TEST_NAMES ${TEST_SOURCES})

if(WIN32)
    set(LAYER_SEPARATOR "\\;")
else()
    set(LAYER_SEPARATOR ":")
endif()
# Layers from Vulkan SDK 1.4 on only read the VK_LAYER_ names, older ones the VK_KHRONOS_VALIDATION_ ones. The shader
# accesses heuristic is what makes the layer check storage buffers and images used by shaders at all.
set(VALIDATION_ENVIRONMENT
    VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true
    VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true
    VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn
    VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG
    VK_LAYER_VALIDATE_SYNC=true
    VK_LAYER_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true
    VK_LAYER_REPORT_FLAGS=error,warn
    VK_LAYER_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG)

# Registers one ctest test. `environment` is the name of a list variable with extra environment entries,
# `extraLayer` is appended below the validation layer.
function(pixelkiln_register_test name labels environment extraLayer)
    add_test(NAME ${name} COMMAND ${ARGN})
    set(testEnvironment ${${environment}})
    set(layers "")
    if(PIXELKILN_TEST_VALIDATION)
        set(layers "VK_LAYER_KHRONOS_validation")
        list(APPEND testEnvironment ${VALIDATION_ENVIRONMENT})
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
    if("window" IN_LIST labels)
        # One window test at a time: they share the screen and the window system.
        set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE)
    endif()
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

foreach(example ComputeBasic)
    pixelkiln_add_test(example.${example} example $<TARGET_FILE:${example}>)
endforeach()

# Fails if a TEST() wasn't picked up by the regex above (e.g. indented) or a name is registered twice.
add_test(NAME test_names_in_sync COMMAND $<TARGET_FILE:PixelKilnTests> --verify ${TEST_NAMES})
set_tests_properties(test_names_in_sync PROPERTIES LABELS "meta")

# Window tests open real windows, so they need a display: opt in with -DPIXELKILN_TEST_WINDOW=ON.
option(PIXELKILN_TEST_WINDOW "Build and register the window/presentation tests (needs a display and GLFW)" OFF)
if(PIXELKILN_TEST_WINDOW)
    if(NOT PIXELKILN_GLFW)
        message(FATAL_ERROR "PIXELKILN_TEST_WINDOW needs GLFW (git submodule update --init, X11 headers on Linux)")
    endif()
    add_executable(PixelKilnWindowTests testMain.cpp windowTests.cpp)
    target_link_libraries(PixelKilnWindowTests PRIVATE PixelKiln glfw)
    target_include_directories(PixelKilnWindowTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    pixelkiln_add_shaders(PixelKilnWindowTests shaders/background.vert shaders/solid.frag)
    pixelkiln_scan_tests(WINDOW_TEST_NAMES ${CMAKE_CURRENT_SOURCE_DIR}/windowTests.cpp)
    foreach(test ${WINDOW_TEST_NAMES})
        pixelkiln_add_test(${test} window $<TARGET_FILE:PixelKilnWindowTests> ${test})
    endforeach()
    foreach(example Particles ReactionDiffusion Mandelbrot TorusKnot)
        pixelkiln_add_test(example.${example} "example;window" $<TARGET_FILE:${example}> --frames 120)
    endforeach()
    add_test(NAME window_test_names_in_sync COMMAND $<TARGET_FILE:PixelKilnWindowTests> --verify ${WINDOW_TEST_NAMES})
    set_tests_properties(window_test_names_in_sync PROPERTIES LABELS "meta")
endif()
