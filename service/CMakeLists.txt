set(SVC_SOURCES
    src/core/main.cpp
    src/core/config.cpp
    src/ingest/filter_listener.cpp
    src/conviction/oracle.cpp
    src/conviction/oracle_battery.cpp
    src/conviction/mode_verify.c
    src/conviction/keystore.cpp
    src/conviction/chain_handler.cpp
    src/conviction/ciphers/sm4.c
    src/conviction/ciphers/seed.c
    src/conviction/ciphers/aria.c
    src/conviction/ciphers/camellia.c
    src/conviction/ciphers/hc128.c
    src/conviction/ciphers/sosemanuk.c
    src/conviction/ciphers/stream.c
    src/recovery/restore.cpp
    src/recovery/journal_restore.cpp
    src/recovery/eviction.cpp
    src/recovery/volume_map.cpp
    src/identity/whitelist.cpp
    src/identity/process_tree.cpp
    src/identity/process_control.cpp
    src/control/installer.cpp
    src/control/status.cpp
    src/control/model_control.cpp
)

add_executable(semantics_ar_service ${SVC_SOURCES})

target_include_directories(semantics_ar_service PRIVATE
    include
    src/conviction
    src/conviction/ciphers)

target_link_libraries(semantics_ar_service PRIVATE
    fltlib
    bcrypt
    crypt32
    wintrust
    advapi32
    ws2_32)

semantics_ar_apply_common_settings(semantics_ar_service)

set_target_properties(semantics_ar_service PROPERTIES
    OUTPUT_NAME "SemanticsARService")