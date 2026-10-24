/**
 * @file storage_volume.h
 * @brief Seam of the storage family — a mountable volume of files.
 *
 * modules/storage decides WHEN a volume is brought up and what the panel is told about it;
 * a driver behind this seam knows HOW: which controller, which pins, which power rail,
 * which filesystem, and what to do when a mount fails. The module never sees an SDMMC host
 * or a FAT mount config, so a board whose files live on something other than a TF socket
 * needs another driver, not a second module.
 *
 * Module-bound like lcd_port.h and audio_sink.h: the owning module resolves it in on_bind()
 * through DriverRegistry::create_storage(), not DriverManager — a volume is neither a sensor
 * nor an actuator, and nothing polls it.
 *
 * No ESP-IDF type appears here, so the header builds in tests/host.
 */

#pragma once

#include <cstdint>

namespace modesp::storage {

/// A mounted filesystem the firmware reads files from.
///
/// Read-only by contract: nothing in this firmware writes to a volume. That is what lets
/// free_bytes() be a number measured once (see below) rather than a thing to poll.
class IStorageVolume {
public:
    virtual ~IStorageVolume() = default;

    /// Bring the volume up: power, bus, filesystem. A false return is NOT fatal — it means
    /// there is no volume to read, and status() then says why in one short line. A board
    /// with an empty slot must still boot and play from the network.
    virtual bool mount() = 0;

    /// Release the filesystem. Safe to call when not mounted.
    virtual void unmount() = 0;

    virtual bool mounted() const = 0;

    /// Where the files are, e.g. "/sd". Valid before mount() too — it is board
    /// configuration, not a property of the medium.
    virtual const char* mount_point() const = 0;

    /// One short line for the panel: "ok", or why not. Never null.
    virtual const char* status() const = 0;

    /// What the medium calls itself (an SD card's CID name), "" when it does not say.
    virtual const char* label() const = 0;

    virtual uint64_t capacity_bytes() const = 0;

    /// Free space as measured AT MOUNT, and deliberately not refreshed.
    ///
    /// On FAT this costs a walk of the whole allocation table the first time it runs on a
    /// volume whose FSINFO is stale — megabytes of reads on a 32 GB card. It was once being
    /// done from the main task every ten seconds while the player streamed from the same
    /// card through the same controller, and the main task is on a five-second watchdog: one
    /// slow scan was not slow software, it was a reboot. Since nothing writes to the volume,
    /// the number cannot change while it is mounted.
    virtual uint64_t free_bytes() const = 0;

    /// Driver type string, as the manifest spells it ("sdmmc").
    virtual const char* name() const = 0;

    virtual bool is_healthy() const = 0;
};

} // namespace modesp::storage
