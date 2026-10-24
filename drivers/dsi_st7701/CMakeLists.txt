# ST7701S MIPI-DSI panel driver (optional via menuconfig; the toggle depends on
# SOC_MIPI_DSI_SUPPORTED, so it only exists on the ESP32-P4).
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/dsi_st7701_driver.cpp"
    PRIV_REQUIRES esp_lcd esp_driver_ledc esp_driver_gpio
)
