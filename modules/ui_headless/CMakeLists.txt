# ui_headless — what ui_lvgl does for a board WITHOUT a display (the ESP32-S3, docs/S3_PORT.md):
# the web's cover and card library (GET /api/cover, /api/library, the ui.cover keys), the web's
# language and colour scheme (ui.lang, ui.theme), and the encoder: volume, play/pause, mute.
# It never builds next to ui_lvgl — project.json's board overlay puts one or the other in.
idf_component_register(
    SRCS "src/ui_headless_module.cpp"
         "src/headless_web.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core modesp_hal
    # cover: the picture the web shows; playlist: the card's index; web_media: the bodies of the
    # two routes, shared with ui_lvgl; modesp_net + esp_http_server: the routes themselves.
    PRIV_REQUIRES cover playlist web_media modesp_net esp_http_server
)
