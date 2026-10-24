/**
 * @file storage_module.h
 * @brief microSD mount and reporting — the first half of a local player.
 *
 * The module decides WHEN the volume is brought up and what the panel is told about it; the
 * bound storage driver (drivers/sdmmc_card) knows HOW. The seam is modesp/hal/storage_volume.h and
 * the volume comes from bindings.json like every other driver-backed peripheral, so this file
 * names neither a slot nor an SDMMC host.
 */

#pragma once

#include "modesp/base_module.h"
#include <cstdint>

namespace modesp { class DriverManager; struct BindingTable; class HAL; }
namespace modesp::storage { class IStorageVolume; }

class StorageModule : public modesp::BaseModule {
public:
    StorageModule();

    void on_bind(modesp::DriverManager& drivers, const modesp::BindingTable& bindings,
                 modesp::HAL& hal) override;
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    /// Mount point of the card, or nullptr when nothing is mounted. The player will ask.
    const char* mount_point() const;
    bool        mounted() const;

private:
    void publish(const char* status);
    void scan(const char* mount_point);

    modesp::storage::IStorageVolume* vol_ = nullptr;
};
