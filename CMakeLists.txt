cmake_minimum_required(VERSION 3.20)
project(semantics_ar LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

enable_testing()

add_subdirectory(engine)
add_subdirectory(tests)
