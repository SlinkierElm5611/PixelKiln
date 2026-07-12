cmake_minimum_required(VERSION 4.2)
project(PixelKiln)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

add_library(PixelKiln STATIC ${SOURCES})

target_include_directories(PixelKiln PUBLIC include)
