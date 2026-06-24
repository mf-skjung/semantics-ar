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
    capture.c
    keystore_persist.c
    commport.c
    state.c
    processnotify.c
    feature.c
    bypassio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/msg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/whitelist.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/src/handshake.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../capture/src/capture.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/battery.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/modes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/gate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/keystore.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/keystore_mgr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src/sha256.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/aes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/des.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/sm4.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/camellia.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/aria.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/seed.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers/stream.c)

add_library(semantics_ar_driver MODULE ${SAR_DRIVER_SOURCES})

set_target_properties(semantics_ar_driver PROPERTIES
    SUFFIX ".sys"
    OUTPUT_NAME "semantics_ar")

target_include_directories(semantics_ar_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../control/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../capture/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/ciphers)

target_compile_definitions(semantics_ar_driver PRIVATE
    _AMD64_
    AMD64
    NTDDI_VERSION=NTDDI_WIN11_GE
    _WIN32_WINNT=0x0A00
    POOL_NX_OPTIN=1)

target_link_libraries(semantics_ar_driver PRIVATE
    fltMgr.lib
    ntoskrnl.lib
    ksecdd.lib)

set_target_properties(semantics_ar_driver PROPERTIES
    LINK_FLAGS "/DRIVER /INTEGRITYCHECK /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /NODEFAULTLIB")
