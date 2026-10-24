/**
 * @file radio_catalog_task.h
 * @brief What the rest of the radio module may ask of the catalog task (src/catalog.cpp).
 */

#pragma once

namespace radio_catalog_task {

/// Counts a play with Radio Browser (GET /json/url/<uuid>), in the background. The service
/// counts one per address, station and day whatever is sent, so a repeat is harmless.
void click(const char* uuid);

}  // namespace radio_catalog_task
