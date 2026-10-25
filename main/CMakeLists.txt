# Module list comes from project.json via generated/modules.cmake (PRODUCT_MODULES) —
# adding/removing a module in project.json is the ONLY step; no manual list here.
# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: during ESP-IDF's requirements-expansion
# pass CMAKE_SOURCE_DIR points at build/ (see components/modesp_hal/CMakeLists.txt).
get_filename_component(_modesp_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_modesp_root}/generated/modules.cmake")   # PRODUCT_MODULES

idf_component_register(
    # userdata_http.cpp: GET/POST /api/userdata — here, because the HTTP
    # server depends on user_store and so user_store cannot depend on the server.
    SRCS "main.cpp" "userdata_http.cpp"
    INCLUDE_DIRS "${_modesp_root}/generated"
    PRIV_REQUIRES modesp_core modesp_services modesp_hal modesp_net ${PRODUCT_MODULES}
                  user_store safe_file esp_http_server
                  hosted_mem net_probe   # esp_hosted frame copies; bench-only UDP probe
                  lwip_dns_compat        # one address for lwIP's SNTP, see its source
)

# Cloud backend: compile-time Kconfig choice (MODESP_CLOUD_NONE links nothing)
if(CONFIG_MODESP_CLOUD_MQTT)
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_mqtt)
endif()

# BLE transport: optional, compile-time Kconfig toggle (menu "ModESP BLE")
if(CONFIG_MODESP_BLE_ENABLE)
    idf_component_get_property(main_lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_link_libraries(${main_lib} PRIVATE idf::modesp_ble)
endif()
