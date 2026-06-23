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
    src/keystore.c)

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
    set_source_files_properties(src/battery.c src/keystore.c
        PROPERTIES COMPILE_OPTIONS "-Wconversion")
endif()
