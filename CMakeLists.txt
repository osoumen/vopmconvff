cmake_minimum_required(VERSION 3.1)

project(vopmconvff C CXX)
set(CMAKE_CXX_STANDARD 11)

if (MSVC)
add_definitions(-D_USE_MATH_DEFINES)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions("/wd4309")
endif ()
add_definitions(-D__LITTLE_ENDIAN__)
set(CMAKE_SUPPRESS_REGENERATION true)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

add_executable(vopmconvff 
    vopmconvff.cpp
)
