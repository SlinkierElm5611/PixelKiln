cmake_minimum_required(VERSION 3.28.3)
project(PixelKiln)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")
if(APPLE)
    # Attaches a CAMetalLayer to an NSView for NATIVE_WINDOW_COCOA_VIEW. Compiled as Objective-C++ through the C++
    # compiler so enabling OBJCXX doesn't change how other projects' .m files (GLFW) are built.
    list(APPEND SOURCES src/metalLayer.mm)
    set_source_files_properties(src/metalLayer.mm PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-xobjective-c++")
endif()

find_package(Vulkan REQUIRED COMPONENTS glslc)

add_library(PixelKiln STATIC ${SOURCES})

# Only include/ is public, Vulkan never reaches users of the library.
target_include_directories(PixelKiln PUBLIC include PRIVATE src)

target_link_libraries(PixelKiln PRIVATE Vulkan::Vulkan)

# Window systems. On Linux each one is compiled in when its headers are available; nothing extra is linked because
# surface creation only hands the application's handles to Vulkan.
if(APPLE)
    target_link_libraries(PixelKiln PRIVATE "-framework AppKit" "-framework QuartzCore")
elseif(UNIX)
    include(CheckIncludeFileCXX)
    check_include_file_cxx(X11/Xlib.h PIXELKILN_HAVE_XLIB)
    check_include_file_cxx(xcb/xcb.h PIXELKILN_HAVE_XCB)
    check_include_file_cxx(wayland-client.h PIXELKILN_HAVE_WAYLAND)
    if(PIXELKILN_HAVE_XLIB)
        target_compile_definitions(PixelKiln PRIVATE PIXELKILN_SURFACE_XLIB)
    endif()
    if(PIXELKILN_HAVE_XCB)
        target_compile_definitions(PixelKiln PRIVATE PIXELKILN_SURFACE_XCB)
    endif()
    if(PIXELKILN_HAVE_WAYLAND)
        target_compile_definitions(PixelKiln PRIVATE PIXELKILN_SURFACE_WAYLAND)
    endif()
endif()

include(cmake/pixelKilnShaders.cmake)

# GLFW (git submodule external/glfw) is only used by the window example and window tests, never by the library.
set(PIXELKILN_GLFW OFF)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/CMakeLists.txt)
    set(PIXELKILN_GLFW ON)
    if(UNIX AND NOT APPLE)
        # GLFW's X11 backend needs these development headers; skip the window targets when they are missing.
        include(CheckIncludeFileCXX)
        foreach(header X11/Xlib.h X11/extensions/Xrandr.h X11/extensions/Xinerama.h X11/Xcursor/Xcursor.h
                       X11/extensions/XInput2.h)
            string(MAKE_C_IDENTIFIER ${header} variable)
            check_include_file_cxx(${header} PIXELKILN_HAVE_${variable})
            if(NOT PIXELKILN_HAVE_${variable})
                set(PIXELKILN_GLFW OFF)
            endif()
        endforeach()
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # X11 also works under XWayland
    endif()
endif()
if(PIXELKILN_GLFW)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/glfw EXCLUDE_FROM_ALL)
else()
    message(STATUS "PixelKiln: GLFW not available (git submodule update --init, plus X11 development headers on "
                   "Linux), skipping the window example and window tests")
endif()

add_subdirectory(examples)

option(PIXELKILN_BUILD_TESTS "Build the PixelKiln ctest suite" ${PROJECT_IS_TOP_LEVEL})
if(PIXELKILN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
