# playlist — what the player is working through, and where it is in it.
#
# A component rather than part of modules/player because two sides need it and a module may
# not depend on another module: the player owns the cursor, modules/ui_lvgl draws the list.
idf_component_register(
    SRCS "src/playlist.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hw_support
)
