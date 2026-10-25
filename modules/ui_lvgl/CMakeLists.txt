# ui_lvgl — LVGL UI over the lcd seam (modesp/hal/lcd_port.h).
# Hardware-agnostic: the panel and touch backends are DRIVERS (drivers/dsi_st7701,
# drivers/gt911) registered via MODESP_REGISTER_LCD; the module resolves them from
# bindings.json in on_bind and only ever sees esp_lcd handles. LVGL's heap lives in
# PSRAM through src/lv_mem_psram.c (CONFIG_LV_USE_CUSTOM_MALLOC).
get_filename_component(_modesp_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Generated LVGL faces (tools/fonts/build_fonts.py). Globbed rather than listed because the
# set is data-driven by that script's FACES/ICONS tables; re-running it must not also require
# editing this file. They are committed, so the firmware build needs neither node nor network.
file(GLOB _ui_fonts "${CMAKE_CURRENT_LIST_DIR}/src/fonts/*.c")

idf_component_register(
    SRCS "src/ui_lvgl_module.cpp"
         "src/ui_theme.cpp"
         "src/ui_m3.cpp"
         "src/ui_m3_widgets.cpp"
         "src/ui_helpers.cpp"
         "src/ui_shell.cpp"
         "src/i18n.cpp"
         # Generated from webui/src/i18n/*.json by tools/i18n/gen_panel_strings.py, committed
         # like the fonts: the strings in four languages, const, in flash.
         "src/i18n_tables.cpp"
         "src/screen_now.cpp"
         "src/screen_src.cpp"
         "src/screen_radio.cpp"
         "src/screen_catalog.cpp"
         "src/ui_search_kb.cpp"
         "src/screen_library.cpp"
         "src/screen_dlna.cpp"
         "src/screen_eq.cpp"
         "src/screen_set.cpp"
         "src/screen_display.cpp"
         "src/screen_xover.cpp"
         "src/screen_speakers.cpp"
         "src/sound_sheet.cpp"
         "src/ui_pick.cpp"
         "src/screen_svc.cpp"
         "src/screen_wifi.cpp"
         "src/screen_bt.cpp"
         "src/net_scan.cpp"
         "src/screen_m3test.cpp"
         "src/ui_screenshot.cpp"
         "src/ui_fit.cpp"
         "src/ui_frame_meter.cpp"
         "src/ui_web_media.cpp"
         "src/ui_scroll_bench.cpp"
         "src/ppa_flush.cpp"
         "src/lv_mem_psram.c"
         ${_ui_fonts}
    INCLUDE_DIRS "include" "src" "${_modesp_root}/generated"
    # modesp_hal is PUBLIC: ui_lvgl_module.h includes the lcd seam (modesp/hal/lcd_port.h).
    REQUIRES modesp_core modesp_hal
    # esp_app_format: the Settings screen prints the running firmware version from the
    # app descriptor rather than a #define, so an OTA image reports itself honestly.
    # audio_dsp: the EQ screen draws its curve with the same coefficients the audio core
    # runs, and reads/writes eq.json through the same parser (components/audio_dsp).
    # modesp_net + esp_http_server + esp_mm: GET /api/screenshot (src/ui_screenshot.cpp) reads the
    # frame buffer behind the web UI's login; GET /api/library and /api/cover
    # (src/ui_web_media.cpp) send the card's index and the Player's cover tile behind it too.
    # bt: the Bluetooth page and the pairing dialog read the satellite's settings through
    # modules/bt/include/bt_settings.h.
    # esp_driver_ppa: the rotated panel's flush turns blocks straight into the frame buffer (src/ppa_flush.cpp).
    PRIV_REQUIRES dlna_client bt esp_lcd esp_driver_ppa esp_timer esp_app_format audio_dsp playlist cover stations user_store
                  modesp_net esp_http_server esp_mm
                  web_media   # the bodies of /api/library and /api/cover (shared with ui_headless)
                  # radio_catalog: the catalog page draws the page, the countries and the genres
                  # the radio module publishes there, and leaves its asks there (radio_catalog_ui.h).
                  radio_catalog
    # lv_mem_psram.c defines the malloc core that liblvgl references; nothing in this
    # component references it, so the archive member is never pulled in by the linker
    # (lvgl/lvgl#8191). Link the whole archive instead of playing with -u per symbol.
    WHOLE_ARCHIVE
)

# The flush copies between the two DPI frame buffers with esp_lcd's own DMA2D frame-buffer copy
# (esp_async_fbcpy, what the DPI driver copies with when use_dma2d is set; src/ppa_flush.cpp), and
# the scroll bench times it. esp_lcd keeps its header in priv_include.
idf_component_get_property(_lcd_dir esp_lcd COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${_lcd_dir}/priv_include")
