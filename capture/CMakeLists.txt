add_library(semantics_ar_capture STATIC
    src/capture.c)

target_include_directories(semantics_ar_capture PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/include")

target_link_libraries(semantics_ar_capture PUBLIC semantics_ar_engine)

set_target_properties(semantics_ar_capture PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

if(MSVC)
    target_compile_options(semantics_ar_capture PRIVATE /W4 /WX)
else()
    target_compile_options(semantics_ar_capture PRIVATE -Wall -Wextra -Werror -Wconversion)
endif()
