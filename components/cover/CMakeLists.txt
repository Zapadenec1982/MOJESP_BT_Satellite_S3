# cover — album artwork as a small RGB565 tile. The P4's JPEG engine decodes and its PPA
# scales, so neither the player task nor LVGL spends CPU on a megapixel image.
# A board without a display keeps the file instead (CONFIG_COVER_DECODE_TILE=n,
# src/cover_bytes.cpp); each of the two .cpp files compiles to nothing in the other mode, and
# the JPEG/PPA components exist (empty) on chips without the hardware, so the requirements
# need no condition.
idf_component_register(
    SRCS "src/cover.cpp" "src/cover_bytes.cpp" "src/cover_soft.c" "src/cover_tone.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_jpeg esp_driver_ppa esp_mm
)
