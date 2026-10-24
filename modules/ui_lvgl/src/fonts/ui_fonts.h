/**
 * @file ui_fonts.h
 * @brief Generated LVGL faces and icon glyphs — DO NOT EDIT.
 *
 * Produced by tools/fonts/build_fonts.py from @fontsource/roboto (SIL OFL 1.1) and
 * Material Symbols Rounded (Apache 2.0, google/material-design-icons). Re-run that
 * script after changing its FACES / ICONS / FILLED tables and commit the result; the
 * firmware build never needs node, pip or the network.
 *
 * Naming: ui_font_<weight>_<px> for text, ui_font_icons_<px> for outlined icons and
 * ui_font_icons_<px>_fill for the filled variants. The UI picks a face through the
 * type roles in ui_m3.h, never by referencing these directly.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(ui_font_500_16);
LV_FONT_DECLARE(ui_font_500_18);
LV_FONT_DECLARE(ui_font_400_18);
LV_FONT_DECLARE(ui_font_400_20);
LV_FONT_DECLARE(ui_font_400_24);
LV_FONT_DECLARE(ui_font_500_32);
LV_FONT_DECLARE(ui_font_600_48);
LV_FONT_DECLARE(ui_font_300_160);

LV_FONT_DECLARE(ui_font_icons_20);
LV_FONT_DECLARE(ui_font_icons_24);
LV_FONT_DECLARE(ui_font_icons_28);
LV_FONT_DECLARE(ui_font_icons_40);
LV_FONT_DECLARE(ui_font_icons_24_fill);
LV_FONT_DECLARE(ui_font_icons_28_fill);
LV_FONT_DECLARE(ui_font_icons_40_fill);

// Material Symbols as UTF-8 literals, usable anywhere LVGL takes text. A glyph
// drawn with a *_fill face is the filled variant; the string is the same.

#define UI_SYM_PLAY         "\xEE\x80\xB7"   // U+E037 play_arrow
#define UI_SYM_PAUSE        "\xEE\x80\xB4"   // U+E034 pause
#define UI_SYM_STOP         "\xEE\x81\x87"   // U+E047 stop
#define UI_SYM_NEXT         "\xEE\x81\x84"   // U+E044 skip_next
#define UI_SYM_PREV         "\xEE\x81\x85"   // U+E045 skip_previous
#define UI_SYM_REPEAT       "\xEE\x81\x80"   // U+E040 repeat
#define UI_SYM_REPEAT_ONE   "\xEE\x81\x81"   // U+E041 repeat_one
#define UI_SYM_SHUFFLE      "\xEE\x81\x83"   // U+E043 shuffle
#define UI_SYM_VOLUME       "\xEE\x81\x90"   // U+E050 volume_up
#define UI_SYM_VOLUME_DOWN  "\xEE\x81\x8D"   // U+E04D volume_down
#define UI_SYM_MUTE         "\xEE\x81\x8F"   // U+E04F volume_off
#define UI_SYM_BLUETOOTH    "\xEE\x86\xA7"   // U+E1A7 bluetooth
#define UI_SYM_RADIO        "\xEE\x80\xBE"   // U+E03E radio
#define UI_SYM_SD           "\xEE\x98\xA3"   // U+E623 sd_card
#define UI_SYM_AIRPLAY      "\xEE\x81\x95"   // U+E055 airplay
#define UI_SYM_CAST         "\xEE\x8C\x87"   // U+E307 cast
#define UI_SYM_AUX          "\xEE\xA2\x90"   // U+E890 input
#define UI_SYM_USB          "\xEE\x87\xA0"   // U+E1E0 usb
#define UI_SYM_SPEAKER      "\xEE\x8C\xAE"   // U+E32E speaker_group
#define UI_SYM_MENU         "\xEE\x97\x92"   // U+E5D2 menu
#define UI_SYM_MENU_OPEN    "\xEE\xA6\xBD"   // U+E9BD menu_open
#define UI_SYM_SEARCH       "\xEE\xBD\xBA"   // U+EF7A search
#define UI_SYM_MOON         "\xEF\x85\x99"   // U+F159 bedtime
#define UI_SYM_POWER        "\xEF\xA3\x87"   // U+F8C7 power_settings_new
#define UI_SYM_BACK         "\xEE\x97\x84"   // U+E5C4 arrow_back
#define UI_SYM_CHEVRON      "\xEE\x97\x8C"   // U+E5CC chevron_right
#define UI_SYM_CHEVRON_LEFT "\xEE\x97\x8B"   // U+E5CB chevron_left
#define UI_SYM_DROPDOWN     "\xEE\x97\x85"   // U+E5C5 arrow_drop_down
#define UI_SYM_REFRESH      "\xEE\x97\x95"   // U+E5D5 refresh
#define UI_SYM_CLOSE        "\xEE\x97\x8D"   // U+E5CD close
#define UI_SYM_CHECK        "\xEE\x99\xA8"   // U+E668 check
#define UI_SYM_CHECK_CIRCLE "\xEF\x82\xBE"   // U+F0BE check_circle
#define UI_SYM_PLAY_CIRCLE  "\xEE\x87\x84"   // U+E1C4 play_circle
#define UI_SYM_EQ           "\xEE\x90\xA9"   // U+E429 tune
#define UI_SYM_SETTINGS     "\xEE\xA2\xB8"   // U+E8B8 settings
#define UI_SYM_MUSIC        "\xEE\x90\x85"   // U+E405 music_note
#define UI_SYM_PLAYLIST     "\xEE\x80\xBD"   // U+E03D queue_music
#define UI_SYM_LIBRARY      "\xEE\x80\xB0"   // U+E030 library_music
#define UI_SYM_FOLDER       "\xEE\x8B\x87"   // U+E2C7 folder
#define UI_SYM_ALBUM        "\xEE\x80\x99"   // U+E019 album
#define UI_SYM_HEART        "\xEE\xA1\xBE"   // U+E87E favorite
#define UI_SYM_ADD          "\xEE\x85\x85"   // U+E145 add
#define UI_SYM_PLAYLIST_ADD "\xEE\x80\xBB"   // U+E03B playlist_add
#define UI_SYM_REMOVE       "\xEE\x85\x9B"   // U+E15B remove
#define UI_SYM_SWAP         "\xEE\xA3\x94"   // U+E8D4 swap_horiz
#define UI_SYM_GRAPHIC_EQ   "\xEE\x86\xB8"   // U+E1B8 graphic_eq
#define UI_SYM_SPEED        "\xEE\xA7\xA4"   // U+E9E4 speed
#define UI_SYM_WIFI         "\xEE\x98\xBE"   // U+E63E wifi
#define UI_SYM_CLOCK        "\xEE\xBF\x96"   // U+EFD6 schedule
#define UI_SYM_SUN          "\xEE\x94\x98"   // U+E518 light_mode
#define UI_SYM_BRIGHTNESS   "\xEE\x8E\xAB"   // U+E3AB brightness_6
#define UI_SYM_PALETTE      "\xEE\x90\x8A"   // U+E40A palette
#define UI_SYM_CONTRAST     "\xEE\xAC\xB7"   // U+EB37 contrast
#define UI_SYM_HUB          "\xEE\xA7\xB4"   // U+E9F4 hub
#define UI_SYM_LANGUAGE     "\xEE\xA8\x87"   // U+EA07 language
#define UI_SYM_UPDATE       "\xEF\x8B\x8D"   // U+F2CD system_update
#define UI_SYM_INFO         "\xEE\xA2\x8E"   // U+E88E info
#define UI_SYM_WARNING      "\xEF\x82\x83"   // U+F083 warning
#define UI_SYM_ERROR        "\xEF\xA2\xB6"   // U+F8B6 error
#define UI_SYM_MEMORY       "\xEE\x8C\xA2"   // U+E322 memory
#define UI_SYM_CPU          "\xEE\x8C\x8D"   // U+E30D developer_board
#define UI_SYM_LINK         "\xEE\x89\x90"   // U+E250 link
#define UI_SYM_SPLIT        "\xEE\x82\xB6"   // U+E0B6 call_split
#define UI_SYM_WIFI_1       "\xEE\xAF\xA4"   // U+EBE4 network_wifi_1_bar
#define UI_SYM_WIFI_2       "\xEE\xAF\x96"   // U+EBD6 network_wifi_2_bar
#define UI_SYM_WIFI_3       "\xEE\xAF\xA1"   // U+EBE1 network_wifi_3_bar
#define UI_SYM_WIFI_4       "\xEF\x81\xA5"   // U+F065 signal_wifi_4_bar
#define UI_SYM_WIFI_OFF     "\xEE\x99\x88"   // U+E648 wifi_off
#define UI_SYM_LOCK         "\xEE\xA2\x99"   // U+E899 lock
#define UI_SYM_BACKSPACE    "\xEE\x85\x8A"   // U+E14A backspace
#define UI_SYM_SHIFT        "\xEE\x97\xB2"   // U+E5F2 shift
#define UI_SYM_VISIBLE      "\xEE\xA3\xB4"   // U+E8F4 visibility
#define UI_SYM_HIDDEN       "\xEE\xA3\xB5"   // U+E8F5 visibility_off
#define UI_SYM_PHONE        "\xEE\x9E\xBA"   // U+E7BA smartphone
#define UI_SYM_DELETE       "\xEE\xA4\xAE"   // U+E92E delete
#define UI_SYM_EDIT         "\xEF\x82\x97"   // U+F097 edit
#define UI_SYM_LINK_OFF     "\xEE\x85\xAF"   // U+E16F link_off
#define UI_SYM_BT_SEARCH    "\xEE\x98\x8F"   // U+E60F bluetooth_searching
#define UI_SYM_BT_OFF       "\xEE\x86\xA9"   // U+E1A9 bluetooth_disabled

#ifdef __cplusplus
}
#endif
