cmake_minimum_required(VERSION 3.23)
project(VoxelRaytracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_shadercross CONFIG QUIET)

if (SDL3_shadercross_FOUND)
    set(SHADERCROSS_TARGET SDL3_shadercross::SDL3_shadercross)
else()
    message(STATUS "SDL3_shadercross not found via config, searching manually")

    find_path(SHADERCROSS_INCLUDE_DIR NAMES SDL3_shadercross/SDL_shadercross.h)
    find_library(SHADERCROSS_LIBRARY NAMES SDL3_shadercross)

    add_library(Manual::SDL3_shadercross INTERFACE IMPORTED)
    target_include_directories(Manual::SDL3_shadercross INTERFACE ${SHADERCROSS_INCLUDE_DIR})
    target_link_libraries(Manual::SDL3_shadercross INTERFACE ${SHADERCROSS_LIBRARY})
    
    set(SHADERCROSS_TARGET Manual::SDL3_shadercross)
endif()

add_executable(VoxelRaytracer src/main.cpp)

target_link_libraries(VoxelRaytracer SDL3::SDL3 ${SHADERCROSS_TARGET})