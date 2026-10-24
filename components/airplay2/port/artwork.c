/**
 * @file artwork.c
 * @brief Cover art, which upstream receives and throws away.
 *
 * A sender told that covers are welcome (md=0,1,2 in the RAOP record, port/mdns_airplay.c)
 * sends each one as a SET_PARAMETER whose body is the image — JPEG or PNG — or with an empty
 * "image/none" body for a track that has none. Upstream's handler looks at the content type,
 * at most flags has_artwork, and drops the bytes (rtsp_handlers.c, handle_set_parameter);
 * nothing in between is a hook.
 *
 * So the one function that sees every request whole, rtsp_dispatch() — called from
 * rtsp_server.c, defined in rtsp_handlers.c — is wrapped at link time (CMakeLists.txt,
 * --wrap=rtsp_dispatch). An image body is handed to the module on the way past, and every
 * request, image or not, then goes to upstream exactly as it arrived. By this point the
 * request is plaintext: an AirPlay 2 session's HAP layer is undone in rtsp_server.c before
 * dispatch.
 */

#include <stdbool.h>
#include <string.h>

#include "airplay2_port.h"
#include "rtsp_handlers.h"
#include "rtsp_message.h"

void airplay_trace_request(int socket, const uint8_t *raw, size_t len);

int __real_rtsp_dispatch(int socket, rtsp_conn_t *conn, const uint8_t *raw_request,
                         size_t raw_len);

int __wrap_rtsp_dispatch(int socket, rtsp_conn_t *conn, const uint8_t *raw_request,
                         size_t raw_len) {
  airplay_trace_request(socket, raw_request, raw_len);
  static const char METHOD[] = "SET_PARAMETER";
  if (airplay2_port_artwork_wanted() && raw_len > sizeof(METHOD) &&
      memcmp(raw_request, METHOD, sizeof(METHOD) - 1) == 0) {
    rtsp_request_t req;
    if (rtsp_request_parse(raw_request, raw_len, &req) == 0 &&
        strncmp(req.content_type, "image/", 6) == 0) {
      const bool none = strstr(req.content_type, "image/none") != NULL;
      if (none || (req.body && req.body_len)) {
        airplay2_port_artwork(none ? NULL : req.body, none ? 0 : req.body_len,
                              req.content_type);
      }
    }
  }
  return __real_rtsp_dispatch(socket, conn, raw_request, raw_len);
}
