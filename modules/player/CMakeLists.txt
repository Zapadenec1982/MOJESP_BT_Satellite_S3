# player — local files from the microSD card (modules/storage), decoded and handed to the
# audio core as a source. Decoding is esp_audio_codec's "simple decoder": one interface for
# MP3, FLAC, WAV and M4A, so the container never reaches this code.
#
# The output clock follows the session's first track (docs/AUDIO.md, "Output rate and sessions");
# a later track at another rate goes through the fixed-ratio converter on the resampler written
# for Bluetooth, which is simpler here than the asynchronous case it was written for.
idf_component_register(
    SRCS "src/player_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal
    PRIV_REQUIRES audio_pipe audio_dsp audio_tags cover playlist safe_file user_store esp_timer espressif__esp_audio_codec
)
