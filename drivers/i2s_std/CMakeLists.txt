# I2S standard-mode stereo transport (optional via menuconfig)
include("${CMAKE_CURRENT_LIST_DIR}/../../tools/cmake/modesp_driver.cmake")
modesp_driver_component(
    SRCS "src/i2s_std_driver.cpp"
    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio esp_timer audio_dsp
)
