add_library(semantics_ar_control STATIC
    src/msg.c
    src/whitelist.c
    src/mode.c
    src/handshake.c)

target_include_directories(semantics_ar_control PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/include")

target_include_directories(semantics_ar_control PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties(semantics_ar_control PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

if(MSVC)
    target_compile_options(semantics_ar_control PRIVATE /W4 /WX)
else()
    target_compile_options(semantics_ar_control PRIVATE -Wall -Wextra -Werror -Wconversion)
endif()
