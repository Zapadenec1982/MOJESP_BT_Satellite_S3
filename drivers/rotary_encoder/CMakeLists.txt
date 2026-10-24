# Rotary encoder (PCNT) sensor driver (optional via menuconfig; toggle depends on SOC_PCNT_SUPPORTED)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/rotary_encoder_driver.cpp"
    PRIV_REQUIRES esp_driver_pcnt esp_driver_gpio
)
