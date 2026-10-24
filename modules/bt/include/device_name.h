/**
 * @file device_name.h
 * @brief The one name the panel goes by: what phones list for Bluetooth, AirPlay and DLNA, and
 *        the drawer's title (docs/UI_REVIEW_2026-10-17.md, finding 13; the customer, 20.10.2026).
 *
 * «Ім'я» on the Bluetooth pages, the glass's and the web's, sets it: the satellite keeps the
 * Bluetooth half, and the panel keeps this copy in NVS for the services that start before the
 * satellite answers, or without one. The bt module keeps it as a rename goes to the satellite,
 * takes the satellite's name the first time there is none (the name somebody gave it there), and
 * gives this one back to a satellite that comes up with another. AirPlay and DLNA read it as they
 * start, so a new name reaches them at the next start; Bluetooth has it at once.
 */

#pragma once

#include <cstddef>

namespace device_name {

/// 32 bytes, the satellite's limit (LINK_PROTOCOL.md, BT_CONFIG_SET), and the NUL.
constexpr size_t CAP = 33;
/// Until a name is kept: system.device_name in project.json, which never reaches SharedState.
constexpr const char* FALLBACK = "P4 Player";

/// The name kept, or FALLBACK while there is none.
void get(char* out, size_t n);
/// Whether a name has been kept.
bool kept();
/// Keep `name`, 1..32 bytes; NVS is written only when it changes.
void keep(const char* name);

}  // namespace device_name
