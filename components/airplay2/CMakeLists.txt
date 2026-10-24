# airplay2 — the AirPlay 2 receiver of rbouteiller/airplay-esp32, vendored (UPSTREAM.md).
#
# upstream/ stays exactly as published; documented port/ copies adapt the protocol. Only
# port/include is public: upstream's headers have names as generic as settings.h and would
# shadow other components' headers if they leaked into anyone else's include path.

set(up "upstream")
set(upstream_srcs
    ${up}/alac_magic_cookie.c
    ${up}/dacp_client.c
    ${up}/audio/audio_receiver.c
    ${up}/audio/audio_stream.c
    ${up}/audio/audio_stream_realtime.c
    ${up}/audio/audio_stream_buffered.c
    ${up}/audio/audio_decoder.c
    ${up}/audio/audio_buffer.c
    ${up}/audio/audio_timing.c
    ${up}/audio/audio_crypto.c
    ${up}/audio/audio_output_common.c
    port/rtsp_server.c
    ${up}/rtsp/rtsp_conn.c
    port/rtsp_message.c
    port/rtsp_handlers.c
    ${up}/rtsp/rtsp_fairplay.c
    ${up}/rtsp/rtsp_crypto.c
    ${up}/rtsp/rtsp_rsa.c
    ${up}/rtsp/rtsp_events.c
    ${up}/plist/plist_xml.c
    port/bplist_parser.c
    port/bplist_builder.c
    ${up}/plist/base64.c
    ${up}/hap/hap.c
    ${up}/hap/hap_pair_verify.c
    ${up}/hap/hap_pair_setup.c
    ${up}/hap/hap_crypto.c
    ${up}/hap/srp.c
    ${up}/hap/tlv8.c
    ${up}/network/ptp_clock.c
    ${up}/network/ntp_clock.c
    ${up}/network/socket_utils.c
)

idf_component_register(
    SRCS ${upstream_srcs}
         "port/airplay2.c"
         "port/mdns_airplay.c"
         "port/audio_output_ring.cpp"
         "port/artwork.c"
         "port/diagnostics.c"
         "port/buffered_audio.c"
    INCLUDE_DIRS "port/include"
    # port/ first: its settings.h and dac.h stand in for upstream's, which are not taken.
    PRIV_INCLUDE_DIRS "port" "${up}" "${up}/audio" "${up}/rtsp" "${up}/plist" "${up}/hap"
                      "${up}/network"
    # audio_pipe brings audio_dsp: the PCM ring the output writes into, and the converter
    # for a clock that could not follow the stream's rate.
    PRIV_REQUIRES audio_pipe esp_timer esp_netif esp_http_client lwip mbedtls nvs_flash
)

# MAC redirection applies to upstream sources and their adapted port copies:
# see port/redirect.h and UPSTREAM.md, "Local changes". Upstream files only — port/airplay2.c
# defines the redirect target and must see the real esp_read_mac.
set_source_files_properties(${upstream_srcs} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/port/redirect.h")

# Cover art: upstream receives it and drops it, and rtsp_dispatch() is the one place that sees
# the image whole. Its caller (rtsp_server.c) is linked against port/artwork.c's wrapper,
# which hands the image on and calls the real one — UPSTREAM.md, "Local changes".
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=rtsp_dispatch"
    "-Wl,--wrap=rtsp_send_response"
    "-Wl,--wrap=rtsp_send_ok"
    "-Wl,--wrap=rtsp_send_http_response")

# Buffered (AirPlay 2) audio: a packet's SSRC names its encoding, and upstream decodes every
# packet whatever it names. port/buffered_audio.c sees each packet where upstream's buffered
# task hands it to the decryptor, and each frame the decoder refuses — UPSTREAM.md, "Local
# changes".
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=audio_crypto_decrypt_buffered"
    "-Wl,--wrap=audio_decoder_decode")
