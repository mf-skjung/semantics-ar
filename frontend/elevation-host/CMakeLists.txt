if(NOT WIN32)
    return()
endif()
if(NOT MSVC)
    return()
endif()

enable_language(CXX)

add_executable(sar_elevation_host WIN32
    SemanticsArElevation.idl
    resources.rc
    src/host_main.cpp
    src/control_object.cpp
    src/class_factory.cpp
    src/registration.cpp
    ../sarapi/src/control_client.c
    ../sarapi/src/server_identity.c)

target_include_directories(sar_elevation_host PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sarapi/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../service"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../control/include"
    "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(sar_elevation_host PRIVATE
    SARAPI_STATIC _UNICODE UNICODE _WIN32_WINNT=0x0A00)

target_link_libraries(sar_elevation_host PRIVATE
    ole32 oleaut32 advapi32 shlwapi rpcrt4)

set_target_properties(sar_elevation_host PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME SemanticsArElevationHost
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_options(sar_elevation_host PRIVATE
    /W4 /WX /guard:cf /guard:ehcont /Qspectre /sdl)
target_link_options(sar_elevation_host PRIVATE
    /ENTRY:wWinMainCRTStartup
    /guard:cf /guard:ehcont /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
