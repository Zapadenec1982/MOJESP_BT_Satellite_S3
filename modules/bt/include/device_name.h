/**
 * @file device_name.h
 * @brief The one name the panel goes by: what phones list for Bluetooth, AirPlay and DLNA, and
 *        the drawer's title (the customer, 20.10.2026).
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

#if __has_include("project_info.h")
#include "project_info.h"   // generated: system.device_name, with the board's overlay
#endif
#ifndef MODESP_DEVICE_NAME
#define MODESP_DEVICE_NAME "P4 Player"
#endif

namespace device_name {

/// 32 bytes, the satellite's limit (LINK_PROTOCOL.md, BT_CONFIG_SET), and the NUL.
constexpr size_t CAP = 33;
/// Until a name is kept: system.device_name in project.json — "P4 Player" on the panel, the
/// board's overlay on the S3 ("S3 Player") — which never reaches SharedState.
constexpr const char* FALLBACK = MODESP_DEVICE_NAME;

/// The name kept, or FALLBACK while there is none.
void get(char* out, size_t n);
/// Whether a name has been kept.
bool kept();
/// Keep `name`, 1..32 bytes; NVS is written only when it changes.
void keep(const char* name);

}  // namespace device_name
