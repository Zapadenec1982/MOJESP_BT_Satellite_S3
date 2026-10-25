/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_dlna.h"
#include "esp_dlna_cmr.h"
#include "esp_log.h"

static const char *TAG = "DLNA_CMR";

/* p4-player: the list upstream ships opens with `http-get:*:*:*` — "send me anything" — and
   then names WMA, AC3, Vorbis, Speex and MP4. This build registers a fixed set of decoders
   (CONCEPT «Прибиті залежності»), so that list is a promise it cannot keep, and a broken one in the worst way: a control
   point that believes it sends AAC, the stream opens, the decoder refuses, and the device sits
   there silent with every log line looking healthy.
   A renderer's sink list is also how Music Assistant decides what to transcode — tell it the
   truth and it converts on the server, where there is a CPU to do it with. The playlist types
   stay: modules/radio resolves .m3u and .pls itself. */
static void *sink_protocol_support = CONST_STR(
    "http-get:*:audio/mpeg:*,http-get:*:audio/mp3:*,http-get:*:audio/x-mpeg:*,"
    "http-get:*:audio/mpeg3:*,"
    "http-get:*:audio/flac:*,http-get:*:audio/x-flac:*,"
    "http-get:*:audio/wav:*,http-get:*:audio/x-wav:*,http-get:*:audio/wave:*,"
    "http-get:*:audio/mpegurl:*,http-get:*:audio/x-mpegurl:*,http-get:*:audio/x-scpls:*");

void esp_dlna_cmr_register_service(upnp_handle_t upnp)
{
    extern const uint8_t ConnectionManager_start[]  asm("_binary_ConnectionManager_xml_start");
    extern const uint8_t ConnectionManager_end[]    asm("_binary_ConnectionManager_xml_end");

    extern const uint8_t EventProtocolCMR_start[]   asm("_binary_EventProtocolCMR_xml_start");
    extern const uint8_t EventProtocolCMR_end[]     asm("_binary_EventProtocolCMR_xml_end");

    // Register ConnectionManager service
    ESP_LOGD(TAG, "Registering ConnectionManager service");
    const service_action_t cmr_action[] = {
        {
            .name = "GetProtocolInfo",
            .num_attrs = 2,
            .attrs = (upnp_attr_t[])
            {
                { .name = "Source", .val = CONST_STR(""),           .type = (ATTR_TYPE_STR | ATTR_CONST | ATTR_RESPONSE) },
                { .name = "Sink",   .val = sink_protocol_support,   .type = (ATTR_TYPE_STR | ATTR_CONST | ATTR_RESPONSE) },
            },
            .callback = NULL,
        },
        {
            .name = "GetCurrentConnectionIDs",
            .num_attrs = 1,
            .attrs = (upnp_attr_t[])
            {
                { .name = "ConnectionIDs", .val = CONST_STR("0"),           .type = (ATTR_TYPE_STR | ATTR_CONST | ATTR_RESPONSE) },
            },
            .callback = NULL,
        },
        { .name = "GetCurrentConnectionInfo", ONE_ATTR_CONST("val", "NOT_IMPLEMENTED"), .callback = NULL },
        { .name = NULL },
    };

    const upnp_service_item_t cmr_service = {
        .name = "ConnectionManager",
        .version = "1",
        .service_desc = {
            .mime_type  = "text/xml",
            .data       = (const char *)ConnectionManager_start,
            .size       = ConnectionManager_end - ConnectionManager_start,
        },
        .notify_template = {
            .mime_type  = "text/xml",
            .data = (const char *)EventProtocolCMR_start,
            .size = EventProtocolCMR_end - EventProtocolCMR_start,
            .path = "/CMR/"
        }
    };
    esp_upnp_register_service(upnp, &cmr_service, cmr_action, NULL);
}
