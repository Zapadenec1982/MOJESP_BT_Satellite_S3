# modesp_services - Phase 2+4+5a+6.5: System services + Config + NVS + Persist + OTA
idf_component_register(
    SRCS
        "src/error_service.cpp"
        "src/system_monitor.cpp"
        "src/watchdog_service.cpp"
        "src/logger_service.cpp"
        "src/log_policy.cpp"
        "src/config_service.cpp"
        "src/nvs_helper.cpp"
        "src/persist_service.cpp"
        "src/ota_handler.cpp"
        "src/device_id.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal jsmn nvs_flash
    # user_store is private: config_service.cpp brings the store up while it mounts, but no
    # header here exposes it. Nothing in this component uses safe_file directly - user_store
    # does, and carries its own dependency.
    PRIV_REQUIRES esp_timer esp_system joltwallet__littlefs esp_http_client app_update mbedtls esp-tls user_store
)

# Доступ до згенерованих headers (state_meta.h)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/generated")
