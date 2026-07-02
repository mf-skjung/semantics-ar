if(NOT WIN32)
    message(STATUS "semantics_ar_elam: skipped (requires the Windows kernel toolchain)")
    return()
endif()

if(NOT DEFINED ENV{WDKContentRoot} AND NOT DEFINED WDK_ROOT)
    message(STATUS "semantics_ar_elam: skipped (WDK not found; set WDK_ROOT or WDKContentRoot)")
    return()
endif()

add_library(semantics_ar_elam MODULE elam.c)

set_target_properties(semantics_ar_elam PROPERTIES
    SUFFIX ".sys"
    OUTPUT_NAME "semantics_ar_elam")

target_compile_definitions(semantics_ar_elam PRIVATE
    _AMD64_
    AMD64
    NTDDI_VERSION=NTDDI_WIN11_GE
    _WIN32_WINNT=0x0A00
    POOL_NX_OPTIN=1)

target_link_libraries(semantics_ar_elam PRIVATE
    ntoskrnl.lib)

set_target_properties(semantics_ar_elam PROPERTIES
    LINK_FLAGS "/DRIVER /INTEGRITYCHECK /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /NODEFAULTLIB")
