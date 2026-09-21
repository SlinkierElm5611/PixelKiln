cmake_minimum_required(VERSION 4.2)
project(PixelKiln)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

find_package(Vulkan REQUIRED COMPONENTS glslc)

add_library(PixelKiln STATIC ${SOURCES})

# Only include/ is public, Vulkan never reaches users of the library.
target_include_directories(PixelKiln PUBLIC include PRIVATE src)

target_link_libraries(PixelKiln PRIVATE Vulkan::Vulkan)

add_subdirectory(examples)
