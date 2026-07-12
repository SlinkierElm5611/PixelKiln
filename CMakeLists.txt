cmake_minimum_required(VERSION 4.2)
project(PixelKiln)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

find_package(Vulkan REQUIRED)

add_library(PixelKiln STATIC ${SOURCES})

include_directories(include)

target_link_libraries(PixelKiln PRIVATE Vulkan::Vulkan)

add_subdirectory(examples)