set(SEMANTICS_AR_COMMON_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../common/include")

function(semantics_ar_apply_common_settings target)
    target_include_directories(${target} PRIVATE "${SEMANTICS_AR_COMMON_INCLUDE}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE)
    endif()
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
endfunction()