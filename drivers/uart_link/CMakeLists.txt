# Byte link to a co-processor over UART (optional via menuconfig).
# esp_driver_gpio is for probe_rx(), which asks the receive pin what it is attached to.
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/uart_link_driver.cpp"
    PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_rom
)
