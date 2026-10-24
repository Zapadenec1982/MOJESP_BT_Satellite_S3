/**
 * @file mdns_airplay.c
 * @brief The receiver's mDNS advertisement — upstream's, minus what the player already owns.
 *
 * Derived from upstream network/mdns_airplay.c (rbouteiller/airplay-esp32 at 811d5f8,
 * Non-Commercial License, Copyright (c) 2026 Remi Bouteiller — see LICENSE). The TXT
 * records follow the boot-selected profile documented in UPSTREAM.md.
 * What changed, and why (UPSTREAM.md, "Local changes"):
 *
 *  - No mdns_init(), no mdns_hostname_set(). modesp_net starts mDNS and names the host;
 *    upstream calls mdns_init() under ESP_ERROR_CHECK, which on a second init is a reboot.
 *  - Callable again. modesp_net frees mDNS and starts it afresh, with its own service only,
 *    when it leaves its AP fallback for the station network (wifi_service.cpp, the AP→STA
 *    probe), and the receiver's services go with it. Calling this again puts them back, and
 *    does nothing when they are still there.
 *
 *    Call it only where mDNS cannot be freed underneath: after mdns_free() the service lock
 *    is gone, and mdns_service_exists() takes it without asking whether there is a server.
 *    The module holds modesp_net's MdnsGuard across registration/removal, the same
 *    guard Wi-Fi uses for mdns_init/free on its event task. mdns_hostname_get() below
 *    additionally handles the ordinary case where mDNS has not started yet.
 *  - Covers asked for at run time: "md" follows airplay2_config_t::artwork (below), and
 *    so do the AirPlay 2 features' metadata bits (mode_policy.h).
 *  - One identity. Upstream takes deviceid from wifi_get_mac_str() and the RAOP instance
 *    name from esp_read_mac(ESP_MAC_WIFI_STA), which the P4 cannot answer (redirect.h).
 *    Both come from the MAC airplay2_start() was given, so they agree with each other and
 *    with what /info and pair-verify say.
 */

#include "esp_log.h"
#include "mdns.h"
#include <stdio.h>
#include <string.h>

#include "airplay2_port.h"
#include "mode_policy.h"
#include "hap.h"
#include "mdns_airplay.h"
#include "rtsp_handlers.h"

static const char *TAG = "mdns_airplay";

// Mode is stable for each receiver session, shared with listener and /info responses.
#define AIRPLAY_SOURCE_VERSION airplay_mode_version(airplay2_port_legacy())

// Flags: 0x4 = audio receiver
#define AIRPLAY_FLAGS "0x4"

// Metadata types advertised in the "md" txt record:
//   0 = text (track title/artist/album), 1 = artwork (cover art images),
//   2 = progress.
// When artwork is disabled, drop "1" so senders do not transmit cover art
// (which can stall the audio pipeline and cause drop-outs on realtime
// streams) while still sending text and progress metadata.
//
// Not upstream's: whether covers are asked for is the module's choice at start
// (airplay2_config_t::artwork), not CONFIG_ENABLE_AIRPLAY_ARTWORK. Upstream's
// option only makes its handler flag has_artwork; the image itself is taken by
// port/artwork.c either way. md is what an AirPlay 1 sender reads; an AirPlay 2
// sender takes the same choice from the features (mode_policy.h, bit 15).
#define AIRPLAY_METADATA_TYPES   (airplay2_port_artwork_wanted() ? "0,1,2" : "0,2")

// Model identifier - AudioAccessory for speaker appearance
// AppleTV3,2 = Apple TV, AudioAccessory5,1 = HomePod mini (speaker)
#define AIRPLAY_MODEL airplay_mode_model(airplay2_port_legacy())

void mdns_airplay_init(void) {
  char host[MDNS_NAME_BUF_LEN];
  if (mdns_hostname_get(host) != ESP_OK) {
    ESP_LOGW(TAG, "mDNS is not running — not advertised");
    return;
  }
  if (mdns_service_exists("_raop", "_tcp", NULL)) {
    return; // still there since the last call — nothing to put back
  }

  const uint8_t *mac = airplay2_port_mac();
  const char *device_name = airplay2_port_name();
  char device_id[18];
  char features_str[32];
  const bool legacy = airplay2_port_legacy();
  const uint64_t features =
      airplay_mode_features(legacy, airplay2_port_artwork_wanted());
  const uint16_t port = airplay_mode_rtsp_port(legacy);
  const char *protocol_version = legacy ? "1" : "2";
  char service_name[80];
  char pk_str[65]; // 32 bytes = 64 hex chars + null

  snprintf(device_id, sizeof(device_id), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Get real Ed25519 public key from HAP module
  const uint8_t *pk = hap_get_public_key();
  for (int i = 0; i < 32; i++) {
    snprintf(pk_str + (size_t)i * 2, 3, "%02x", pk[i]);
  }

  // Format features as "lo,hi" hex string
  snprintf(features_str, sizeof(features_str), "0x%X,0x%X", (unsigned)(features & UINT32_MAX),
           (unsigned)(features >> 32));

  // Create service name for RAOP: <mac>@<name>
  snprintf(service_name, sizeof(service_name), "%02X%02X%02X%02X%02X%02X@%s",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], device_name);

  if (!legacy) {
    // ========================================
    // _airplay._tcp service (port 7000)
    // Only registered for AirPlay 2 mode
    // ========================================
    mdns_txt_item_t airplay_txt[] = {
        {"deviceid", device_id},
        {"features", features_str},
        {"flags", AIRPLAY_FLAGS},
        {"model", AIRPLAY_MODEL},
        {"pk", pk_str},
        {"pi", "00000000-0000-0000-0000-000000000000"}, // Pairing identity UUID
        {"srcvers", AIRPLAY_SOURCE_VERSION},
        {"vv", protocol_version},
        {"acl", "0"},
    };

    if (!mdns_service_exists("_airplay", "_tcp", NULL)) {
      esp_err_t err =
          mdns_service_add(device_name, "_airplay", "_tcp", port, airplay_txt,
                           sizeof(airplay_txt) / sizeof(airplay_txt[0]));
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add _airplay._tcp service: %s",
                 esp_err_to_name(err));
      }
    }
  }

  // ========================================
  // _raop._tcp service (5000 in legacy mode, 7000 in AirPlay 2 mode)
  // RAOP = Remote Audio Output Protocol
  // Service name format: <MAC>@<DeviceName>
  // ========================================
  mdns_txt_item_t *raop_txt;
  size_t raop_count;
  // AirPlay v1 (classic RAOP): match squeezelite-esp32 txt record format.
  // No features, no pk, no HAP pairing — just classic RAOP fields.
  mdns_txt_item_t legacy_txt[] = {
      {"am", AIRPLAY_MODEL},
      {"tp", "UDP"},                  // Transport protocol
      {"sm", "false"},                // Sharing mode
      {"sv", "false"},                // Server version (unused)
      {"ek", "1"},                    // Encryption key available
      {"et", "0,1"},                  // Encryption types: none, RSA
      {"md", AIRPLAY_METADATA_TYPES}, // Metadata types
      {"cn", "0,1"},                  // Audio codecs: PCM, ALAC
      {"ch", "2"},                    // Channels
      {"ss", "16"},                   // Sample size (bits)
      {"sr", "44100"},                // Sample rate
      {"vn", "3"},                    // Version number
      {"txtvers", "1"},               // TXT record version
  };

  // Dual-mode: include et=1 (RSA) so RAOP-only clients (TuneBlade, AirMusic,
  // shairtunes2, etc.) accept the advertisement, while keeping et=3,5 for
  // AirPlay 2 FairPlay/MFi-SAP. ek=1 advertises that an RSA-encrypted key
  // can be supplied via SDP rsaaeskey: at ANNOUNCE time.
  mdns_txt_item_t modern_txt[] = {
      {"am", AIRPLAY_MODEL},
      {"cn", "0,1,2,3"},              // Audio codecs: PCM, ALAC, AAC, AAC-ELD
      {"da", "true"},                 // Digest auth
      {"ek", "1"},                    // Encryption key available (RSA)
      {"et", "0,1,3,5"},              // Encryption types
      {"ft", features_str},           // Features (same as airplay)
      {"md", AIRPLAY_METADATA_TYPES}, // Metadata types
      {"pk", pk_str},                 // Public key
      {"sf", AIRPLAY_FLAGS},          // Status flags
      {"tp", "UDP"},                  // Transport protocol
      {"vn", "65537"},                // Version number
      {"vs", AIRPLAY_SOURCE_VERSION},
      {"vv", protocol_version},
  };
  raop_txt = legacy ? legacy_txt : modern_txt;
  raop_count = legacy ? sizeof(legacy_txt) / sizeof(legacy_txt[0])
                      : sizeof(modern_txt) / sizeof(modern_txt[0]);

  esp_err_t err_raop =
      mdns_service_add(service_name, "_raop", "_tcp", port, raop_txt,
                       raop_count);
  if (err_raop != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add _raop._tcp service: %s",
             esp_err_to_name(err_raop));
  } else {
    ESP_LOGI(TAG, "advertised as \"%s\" (deviceid %s)", device_name, device_id);
  }
}

// Not upstream's: it never takes itself off the network. Same guard as above.
void mdns_airplay_remove(void) {
  char host[MDNS_NAME_BUF_LEN];
  if (mdns_hostname_get(host) != ESP_OK) {
    return; // no mDNS, nothing advertised
  }
  if (mdns_service_exists("_airplay", "_tcp", NULL)) {
    mdns_service_remove("_airplay", "_tcp");
  }
  if (mdns_service_exists("_raop", "_tcp", NULL)) {
    mdns_service_remove("_raop", "_tcp");
  }
  ESP_LOGI(TAG, "no longer advertised");
}
