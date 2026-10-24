/**
 * @file airplay2.h
 * @brief The whole of what the player sees of the AirPlay 2 receiver.
 *
 * The receiver is upstream code (see ../../UPSTREAM.md) and speaks in upstream's own
 * types; none of those headers leave this component, because several of them have names
 * as generic as "settings.h" and "dac.h". This is the seam instead: start it, hear what it
 * does, ask it to pause the phone.
 *
 * Audio does not come through here. It goes into audio_core::pcm_ring() under
 * PcmRing::OWNER_AIRPLAY, and the module that selects the source decides whether the ring
 * is listening.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIRPLAY2_EV_CONNECTED,     ///< a sender opened a session
    AIRPLAY2_EV_PLAYING,       ///< audio is flowing (first RECORD, or a resume)
    AIRPLAY2_EV_PAUSED,        ///< the sender paused, or the stream was torn down
    AIRPLAY2_EV_DISCONNECTED,  ///< the session is gone
    AIRPLAY2_EV_METADATA,      ///< title / artist / album / duration / position changed
    AIRPLAY2_EV_VOLUME,        ///< the sender moved its volume slider
    /// The cover of what is playing: `image`/`image_len`, JPEG or PNG as `image_type` says.
    /// Zero bytes is the sender saying this track has none ("image/none"). Only sent when
    /// airplay2_config_t::artwork asked for covers.
    AIRPLAY2_EV_ARTWORK,
} airplay2_event_t;

/// Valid for the duration of the callback only. Strings are never NULL.
typedef struct {
    const char* title;
    const char* artist;
    const char* album;
    uint32_t    duration_s;    ///< 0 when the sender did not say
    uint32_t    position_s;
    /// AIRPLAY2_EV_VOLUME: the sender's volume in its own scale, -30..0 dB, and -144 for
    /// its mute. Not converted here: what a dB means on this amplifier is the module's call.
    float       volume_db;
    /// AIRPLAY2_EV_ARTWORK: the image as it arrived. Points into the RTSP buffer — copy it.
    const uint8_t* image;
    size_t         image_len;
    const char*    image_type;
} airplay2_event_data_t;

/// Called on the receiver's own tasks. Keep it short and never block: an RTSP connection
/// waits for it, and so does the phone at the other end.
typedef void (*airplay2_listener_t)(airplay2_event_t ev, const airplay2_event_data_t* d,
                                    void* ctx);

typedef struct {
    const char*          name;       ///< what the phone lists, UTF-8, e.g. "P4 Player"
    uint8_t              mac[6];     ///< the device identity; see port/redirect.h
    float                volume_db;  ///< what to report before the phone has said anything
    /// Ask senders for cover art (the RAOP record's md=0,1,2) and deliver it as
    /// AIRPLAY2_EV_ARTWORK. Upstream receives covers and throws them away; port/artwork.c.
    bool                 artwork;
    bool                 legacy_mode; ///< initial profile; classic RAOP when true
    airplay2_listener_t  listener;
    void*                ctx;
} airplay2_config_t;

/// Bring the receiver up: PTP, HAP, the receiver, the output, RTSP. Call once the
/// network is up. A second call is a no-op returning ESP_OK; the name and identity cannot
/// change without a reboot, because the phone has already paired with them.
esp_err_t airplay2_start(const airplay2_config_t* cfg);
bool      airplay2_started(void);

/// Switch only the RTSP receiver profile, disconnecting AirPlay clients. Call from one
/// lifecycle worker (may block), with mDNS removed and no concurrent start/reconfigure.
/// HAP identity, audio allocations and the output worker remain initialized. On failure
/// the old listener is restored when possible; started() reports actual availability.
/// Advertise again on the network-owning main loop only after this returns.
esp_err_t airplay2_set_legacy_mode(bool legacy);
bool      airplay2_legacy_mode(void);

/// Stop listening for senders (airplay.enabled off): every client is disconnected and the RTSP
/// listener stops, so no session, old or new, until airplay2_set_legacy_mode() starts it again
/// with the profile to use. HAP identity, audio allocations and the output worker stay, as
/// across a profile switch. The same rules as that switch: one lifecycle worker (may block),
/// mDNS removed first. started() is false afterwards.
esp_err_t airplay2_stop_listening(void);

/// (Re)advertise the receiver in mDNS. Separate from start because the network stack frees
/// mDNS and starts it again with its own service only when it moves from its AP fallback to
/// the station network, and the receiver's services go with it. Idempotent. Caller holds
/// the same mDNS lifecycle guard as the network's init/free for the whole operation.
void      airplay2_advertise(void);
/// Take the receiver out of mDNS: phones stop listing it. The server keeps running; turning
/// AirPlay off stops it as well, with airplay2_stop_listening().
void      airplay2_unadvertise(void);

/// Whether the receiver's audio goes into the PCM ring. The module says true while
/// audio.source is AirPlay and false otherwise. Off, the receiver keeps its session, its
/// clock and its place in the stream, reads at the output rate and drops what it reads —
/// what a speaker switched to another input does — so switching back resumes in step.
void      airplay2_set_output(bool on);

/// Ask the sender to pause, for when the panel picks another source. Only an AirPlay 1
/// sender can be asked: that is DACP, and a phone in AirPlay 2 mode does not offer it — it
/// takes remote commands over MRP, which upstream does not implement. Such a sender keeps
/// sending, and the ring, no longer owned by the receiver, drops what it sends. Returns
/// whether a request actually went out.
bool      airplay2_pause_sender(void);

/// What the panel's transport row can ask of a sender: DACP's three commands. DACP has one
/// toggle where a remote has play and pause; which one a press means is the caller's call.
typedef enum {
    AIRPLAY2_REMOTE_PLAYPAUSE,
    AIRPLAY2_REMOTE_NEXT,
    AIRPLAY2_REMOTE_PREV,
} airplay2_remote_t;

/// Whether the sender gave the receiver a remote: DACP-ID and Active-Remote in its RTSP
/// requests. AirPlay 1 senders do — TuneBlade, Music Assistant, iTunes, and an iPhone or a
/// Mac when the panel runs in AirPlay 1 mode. In AirPlay 2 mode a phone offers none. Stays
/// true through an AirPlay 1 pause, while the receiver holds the slot for the phone to come
/// back: that is when the remote's play matters most.
bool      airplay2_remote_ready(void);

/// Send one command to that remote. False when there is none. Never waits on the network:
/// the DACP client queues the request for its own task.
bool      airplay2_remote(airplay2_remote_t cmd);

/// The room's volume, in the sender's scale (-30..0 dB, -144 for mute). GET /info announces it
/// as initialVolume, so a sender starts its slider there and sends no level of its own until
/// somebody moves it (the behaviour UxPlay documents for its clients) — instead of restating
/// whatever it last used here. Emits nothing: it is the panel's level, not the phone's.
void      airplay2_report_volume(float volume_db);

/// Whether a session is open, and whether its audio is flowing.
bool      airplay2_connected(void);
bool      airplay2_playing(void);

#ifdef __cplusplus
}
#endif
