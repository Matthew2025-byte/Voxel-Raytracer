cmake_minimum_required(VERSION 4.4.2)
project(VoxelRaytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_shadercross CONFIG REQUIRED)

add_executable(VoxelRaytracer src/main.cpp)

target_link_libraries(VoxelRaytracer SDL3::SDL3 SDL3_shadercross::SDL3_shadercross)