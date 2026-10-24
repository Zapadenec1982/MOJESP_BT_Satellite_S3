# GT911 I2C touch-controller driver (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/gt911_driver.cpp"
    PRIV_REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio
)
