/**
 * @file sdmmc_card_driver.h
 * @brief microSD/TF slot on the SDMMC controller (storage seam: IStorageVolume).
 *
 * One board `sdmmc_slots` entry -> one driver instance. Pins, width, clock, mount point and
 * the on-chip LDO channel come from board.json (SdmmcSlotConfig); everything about HOW the
 * card is brought up — the power cycle between attempts, the 400 kHz retry that tells a bad
 * format from a marginal bus, and the host init/deinit no-op that keeps ESP-Hosted alive —
 * lives in the .cpp with the evidence for it.
 */

#pragma once

#include "modesp/hal/storage_volume.h"
#include "modesp/hal/hal_types.h"

class SdmmcCardDriver : public modesp::storage::IStorageVolume {
public:
    void configure(const modesp::SdmmcSlotConfig* slot);

    bool mount() override;
    void unmount() override;
    bool mounted() const override { return mounted_; }
    const char* mount_point() const override;
    const char* status() const override { return status_; }
    const char* label() const override { return label_; }
    uint64_t capacity_bytes() const override { return capacity_b_; }
    uint64_t free_bytes() const override { return free_b_; }
    const char* name() const override { return "sdmmc_card"; }
    bool is_healthy() const override { return mounted_; }

private:
    /// Free space, once, right after a successful mount. See IStorageVolume::free_bytes().
    void measure_free();

    const modesp::SdmmcSlotConfig* cfg_ = nullptr;
    /// sdmmc_card_t is a typedef of an anonymous struct, so it cannot be forward-declared.
    /// Held opaquely rather than dragging the SDMMC headers into every translation unit
    /// that includes this one.
    void*    card_ = nullptr;
    /// sd_pwr_ctrl_handle_t, held opaquely for the same reason.
    void*    ldo_  = nullptr;          ///< on-chip LDO feeding the SD IO rail
    bool     mounted_ = false;
    uint64_t capacity_b_ = 0;
    uint64_t free_b_     = 0;
    /// Short enough for the panel, long enough for an esp_err name. Never null.
    char     status_[32] = "no slot";
    char     label_[16]  = "";         ///< the card's CID name
};
