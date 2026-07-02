if(NOT WIN32)
    return()
endif()

add_library(sarapi SHARED
    src/posture_client.c
    src/events_client.c
    src/server_identity.c
    src/control_client.c)

target_include_directories(sarapi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../service"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../control/include")

target_link_libraries(sarapi PRIVATE advapi32)

target_compile_definitions(sarapi PRIVATE
    SARAPI_EXPORTS _UNICODE UNICODE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)

set_target_properties(sarapi PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    OUTPUT_NAME sarapi)

if(MSVC)
    target_compile_options(sarapi PRIVATE /W4 /WX /guard:cf /guard:ehcont /Qspectre /sdl)
    target_link_options(sarapi PRIVATE
        /guard:cf /guard:ehcont /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
endif()
