if(NOT WIN32)
    message(STATUS "semantics_ar driver: skipped (requires the Windows kernel toolchain)")
    return()
endif()

if(NOT DEFINED ENV{WDKContentRoot} AND NOT DEFINED WDK_ROOT)
    message(STATUS "semantics_ar driver: skipped (WDK not found; set WDK_ROOT or WDKContentRoot)")
    return()
endif()

set(SAR_DRIVER_SOURCES
    driver.c
    operations.c
    seam.c
    commport.c
    state.c
    processnotify.c
    feature.c
    bypassio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/msg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/whitelist.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/handshake.c)

add_library(semantics_ar_driver MODULE ${SAR_DRIVER_SOURCES})

set_target_properties(semantics_ar_driver PROPERTIES
    SUFFIX ".sys"
    OUTPUT_NAME "semantics_ar")

target_include_directories(semantics_ar_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include)

target_compile_definitions(semantics_ar_driver PRIVATE
    _AMD64_
    AMD64
    NTDDI_VERSION=NTDDI_WIN10_RS3
    _WIN32_WINNT=0x0A00
    POOL_NX_OPTIN=1)

target_link_libraries(semantics_ar_driver PRIVATE
    fltMgr.lib
    ntoskrnl.lib
    ksecdd.lib)

set_target_properties(semantics_ar_driver PROPERTIES
    LINK_FLAGS "/DRIVER /INTEGRITYCHECK /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /NODEFAULTLIB")
