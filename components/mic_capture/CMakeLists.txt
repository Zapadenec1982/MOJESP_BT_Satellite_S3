# Bench capture is opt-in and leaves the codec powered down until explicitly armed.
option(MODESP_MIC_CAPTURE "Enable onboard ES8311 click capture over USB console" OFF)
# mic_input (the microphone as a measuring input: the port meter) is always built; the bench
# capture only on request.
set(_sources "src/mic_input.cpp")
if(MODESP_MIC_CAPTURE)
    list(APPEND _sources "src/mic_capture.cpp")
endif()
# Requirements must be unconditional: IDF's early expansion pass does not have
# the CMake option cache, even when the later source-selection pass has it.
idf_component_register(SRCS ${_sources} INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c PRIV_REQUIRES esp_driver_i2s esp_driver_usb_serial_jtag esp_timer esp_hw_support espressif__esp_codec_dev)
if(MODESP_MIC_CAPTURE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MODESP_MIC_CAPTURE=1)
endif()
