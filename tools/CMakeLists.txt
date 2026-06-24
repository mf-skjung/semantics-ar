if(NOT WIN32)
    message(STATUS "semantics_ar tools: skipped (require the Windows SDK)")
    return()
endif()

add_executable(provision_handshake_key provision_handshake_key.c)
set_target_properties(provision_handshake_key PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_definitions(provision_handshake_key PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(provision_handshake_key PRIVATE /W4 /WX)
endif()
target_link_libraries(provision_handshake_key PRIVATE ncrypt bcrypt crypt32 advapi32)
