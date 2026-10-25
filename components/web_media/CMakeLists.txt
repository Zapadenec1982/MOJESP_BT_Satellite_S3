# web_media — the bodies of GET /api/library and GET /api/cover (include/web_media.h).
#
# Header-only and free of IDF and LVGL, so the host suite holds them to their shape
# (tests/host/test_web_media.cpp). A component rather than part of a module because two modules
# serve those routes and a module may not depend on another: ui_lvgl on the P4 panel, ui_headless
# on a board without a display. One build has one of them.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES modesp_core   # modesp/json_escape.h
)
