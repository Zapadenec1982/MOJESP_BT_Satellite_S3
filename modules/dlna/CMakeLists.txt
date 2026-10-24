# dlna — the UPnP MediaRenderer's control plane. No audio: a pushed URL goes to the network
# stream engine through SharedState (`radio.play_url`), like every other cross-module command.
idf_component_register(
    SRCS "src/dlna_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
    PRIV_REQUIRES dlna_client esp_dlna audio_dsp modesp_net modesp_services esp_http_server bt   # bt: device_name.h
                  espressif__esp_media_protocols espressif__media_lib_sal
)
