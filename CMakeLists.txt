cmake_minimum_required(VERSION 3.20)
project(semantics_ar LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "semantics-ar service builds on Windows only")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(SemanticsARConfig)

add_subdirectory(service)