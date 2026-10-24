/**
 * @file dlna_module.h
 * @brief DLNA/UPnP MediaRenderer: the device other people's apps push music to.
 */

#pragma once

#include "modesp/base_module.h"
#include <cstdint>

class DlnaModule : public modesp::BaseModule {
public:
    DlnaModule();
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // SharedState, reachable from the HTTPD task.
    //
    // The UPnP callback is a C function pointer, so it cannot be a member and cannot reach
    // BaseModule's protected readers. These four wrappers are that seam and nothing more.
    // Crossing tasks here is safe because SharedState is mutex-guarded — which is also why
    // only these four, and not anything that blocks, may be called from there.
    int32_t state_int(const char* key, int32_t def) const  { return read_int(key, def); }
    bool    state_flag(const char* key, bool def) const    { return read_bool(key, def); }
    void    put_int(const char* key, int32_t value)        { state_set(key, value); }
    void    put_flag(const char* key, bool value)          { state_set(key, value); }
    void    put_text(const char* key, const char* value)   { state_set(key, value); }

private:
    uint32_t poll_ms_ = 0;      ///< pacing for the cheap state mirror
    int32_t  last_volume_ = -1;
    int8_t   last_mute_   = -1;
    bool     last_enabled_ = true;
};
