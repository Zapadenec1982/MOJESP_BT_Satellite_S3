#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Success guarantees that the listener and both RTSP client tasks have finished
// using their session state. A timeout leaves shutdown requested and prevents a
// subsequent start until the remaining tasks have actually exited.
esp_err_t rtsp_server_stop_checked(void);

#ifdef __cplusplus
}
#endif
