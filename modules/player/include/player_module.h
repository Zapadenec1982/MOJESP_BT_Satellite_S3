/**
 * @file player_module.h
 * @brief Local playback from the microSD card.
 */

#pragma once

#include "modesp/base_module.h"
#include <cstdint>

namespace modesp { class DriverManager; struct BindingTable; class HAL; }

class PlayerModule : public modesp::BaseModule {
public:
    PlayerModule();
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

private:
    uint32_t report_ms_ = 0;
    bool     restored_  = false;   ///< the last folder is re-queued once, on the first pass
    char     last_dir_[256] = "";  ///< what /data/last_dir holds, to write only on a change
    bool     playing_   = false;   ///< this module asked for the output and still has it
    /// Set when the output was lent to another source rather than given up by the user,
    /// with what was playing at the moment it was lent. Without it, choosing the card again
    /// leaves a stopped player and looks exactly like "the card does not play".
    bool     yielded_   = false;
    char     yield_track_[256] = "";
    int32_t  yield_pos_ = 0;
    /// Whether the card owned the output on the last pass, and whether there has been a last
    /// pass at all: a change of source TO the card is a choice to listen to it, the source
    /// found at power-on is not (see restored_).
    bool     was_ours_  = false;
    bool     seen_source_ = false;
};
