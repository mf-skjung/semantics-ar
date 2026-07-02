cmake_minimum_required(VERSION 3.20)
project(semantics_ar LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

enable_testing()

add_subdirectory(engine)
add_subdirectory(control)
add_subdirectory(capture)
add_subdirectory(tests)

if(WIN32)
    add_subdirectory(tools)
    add_subdirectory(driver)
    add_subdirectory(service)
    add_subdirectory(frontend/sarapi)
endif()
