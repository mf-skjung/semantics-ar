if(NOT WIN32)
    return()
endif()

add_executable(semantics_ar_service
    main.c
    commclient.c
    control.c
    pipe_server.c
    recovery.c
    identity.c)

target_include_directories(semantics_ar_service PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../control/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/host")

target_link_libraries(semantics_ar_service PRIVATE
    semantics_ar_recovery_host
    semantics_ar_control
    fltlib
    bcrypt
    ncrypt
    wintrust
    crypt32
    advapi32)

set_target_properties(semantics_ar_service PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    WIN32_EXECUTABLE OFF)

if(MSVC)
    target_compile_definitions(semantics_ar_service PRIVATE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN)
    target_compile_options(semantics_ar_service PRIVATE /W4 /WX)
endif()
