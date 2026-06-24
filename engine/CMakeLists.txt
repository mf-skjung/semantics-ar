add_library(semantics_ar_engine STATIC
    ciphers/aes.c
    ciphers/des.c
    ciphers/sm4.c
    ciphers/camellia.c
    ciphers/aria.c
    ciphers/seed.c
    ciphers/stream.c
    src/modes.c
    src/battery.c
    src/sha256.c
    src/keystore.c
    src/keystore_mgr.c
    src/recover.c
    src/gate.c)

target_include_directories(semantics_ar_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ciphers"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties(semantics_ar_engine PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

if(MSVC)
    target_compile_options(semantics_ar_engine PRIVATE /W4 /WX)
else()
    target_compile_options(semantics_ar_engine PRIVATE -Wall -Wextra -Werror)
    set_source_files_properties(src/battery.c src/keystore.c src/gate.c
        PROPERTIES COMPILE_OPTIONS "-Wconversion")
    set_source_files_properties(src/keystore_mgr.c
        PROPERTIES COMPILE_OPTIONS "-Wconversion")
endif()

if(NOT WIN32)
    add_library(semantics_ar_recovery_host STATIC
        host/recover_file.c)

    target_include_directories(semantics_ar_recovery_host PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/host"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common/include")

    target_link_libraries(semantics_ar_recovery_host PUBLIC semantics_ar_engine)

    target_compile_definitions(semantics_ar_recovery_host PRIVATE
        _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)

    set_target_properties(semantics_ar_recovery_host PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON)

    target_compile_options(semantics_ar_recovery_host PRIVATE -Wall -Wextra -Werror)
endif()
