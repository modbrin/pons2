cmake_minimum_required(VERSION 3.10)
project(pons2_vulkan)

set(CMAKE_CXX_STANDARD 20)
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic -Wno-unknown-pragmas -Wconversion -Wsign-conversion)
endif()

# include_directories(libs/glad/include)

find_package(Vulkan REQUIRED FATAL_ERROR)

find_package(SDL2 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
# find_package(freetype CONFIG REQUIRED)

add_executable(pons2 src/helpers.hpp src/main.cpp)

target_link_libraries(pons2 PRIVATE Vulkan::Vulkan SDL2)
# target_link_libraries(pons2 PRIVATE freetype)

if(WIN32)
target_link_libraries(pons2 PRIVATE assimp::assimp)
elseif(UNIX)
target_link_libraries(pons2 PRIVATE assimp)
endif()