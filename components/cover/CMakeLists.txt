# cover — album artwork as a small RGB565 tile. The P4's JPEG engine decodes and its PPA
# scales, so neither the player task nor LVGL spends CPU on a megapixel image.
idf_component_register(
    SRCS "src/cover.cpp" "src/cover_soft.c" "src/cover_tone.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_jpeg esp_driver_ppa esp_mm
)
