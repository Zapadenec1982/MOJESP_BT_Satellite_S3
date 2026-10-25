#pragma once
/**
 * @file bt_settings.h
 * @brief What the panel shows of the satellite's Bluetooth settings, and what it may ask for.
 *
 * Link protocol 1.8. The satellite keeps the settings (radio, visibility, auto-reconnect,
 * codecs, its name) and the paired phones; this is the copy the screen reads, and the queue
 * the screen's requests wait in until the bt module's main-loop tick puts them on the wire.
 *
 * Why not SharedState: a list of sixteen phones with names does not fit a key, a pairing
 * request has to reach the screen with its address, and the requests carry a name and an
 * address that do not fit a command's int. So the state lives here, behind a spinlock, the
 * way dlna_client publishes its browser. The web reads the same view through GET /api/bt and
 * asks through the same queue (bt_web.h, A5); the view's revision and the pairing request are
 * published as bt.* keys so every web tab hears of a change (modules/bt/manifest.json).
 *
 * Threads: the bt module writes the view (main task); the screen reads it (LVGL task) and
 * posts requests (LVGL task); the bt module takes the requests (main task). Every call here
 * is safe from either.
 */

#include <cstddef>
#include <cstdint>

namespace bt_settings {

constexpr size_t BONDS    = 16;   ///< sat::BOND_MAX
constexpr size_t NAME_CAP = 33;   ///< sat::BT_NAME_MAX + NUL

struct Bond {
    uint8_t addr[6] = {};
    uint8_t flags = 0;             ///< sat::BOND_CONNECTED | sat::BOND_LAST
    char    name[NAME_CAP] = {};
};

/// Who answered a pairing request. The question shows on the panel and in every open web tab
/// at once; the first answer is the one sent.
enum class Source : uint8_t { NONE = 0, PANEL = 1, WEB = 2 };

/// A pairing in progress, or the answer to the last one.
struct Pairing {
    uint32_t seq     = 0;         ///< bumps on every new request; 0 = none since boot
    bool     active  = false;     ///< waiting: the dialog is up
    uint8_t  addr[6] = {};
    uint8_t  kind    = 0;         ///< sat::PAIR_NUMERIC / PAIR_CONSENT / PAIR_PASSKEY
    uint32_t passkey = 0;
    char     name[NAME_CAP] = {};
    /// The result of request `seq` once it came: sat::PAIR_OK .. PAIR_FAILED, or 0xFF.
    uint8_t  result  = 0xFF;
    /// Who answered request `seq`, and how — so the other place can say "answered there".
    Source   by       = Source::NONE;
    bool     accepted = false;
};

struct View {
    uint32_t revision = 0;        ///< bumps on every change below (not on a pairing)
    bool     linked   = false;    ///< a satellite answered HELLO
    bool     settings = false;    ///< ... and speaks 1.8 with DEV_CAP_BT_SETTINGS
    bool     known    = false;    ///< a BT_CONFIG has arrived since the link came up
    bool     radio = false, discoverable = false, auto_reconnect = false;
    uint8_t  codecs_supported = 0, codecs_enabled = 0;
    char     name[NAME_CAP] = {};
    uint8_t  total = 0;           ///< bonds on the satellite, maybe more than `count`
    uint8_t  count = 0;
    Bond     bonds[BONDS];
};

// ── reading (the screen) ──

/// The revision, cheaply: copy the view only when it moved.
uint32_t revision();
/// Copy the whole view. ~700 bytes; call it when revision() moved.
void     snapshot(View& out);
/// The pairing, for the shell's dialog, which looks every refresh.
void     pairing(Pairing& out);

/// The switches alone — what the bt module needs to fill a BT_CONFIG_SET, on a stack that
/// cannot hold a whole View.
struct Config {
    bool    known = false, radio = false, discoverable = false, auto_reconnect = false;
    uint8_t codecs_enabled = 0;
};
void     config(Config& out);
/// The name phones see, "" until a BT_CONFIG has told it — for the Player's line without a phone.
void     name(char* out, size_t n);

// ── requests (the screen) ──
//
// One of each kind waits at a time; a newer one replaces an older one that was not yet sent.
// All are dropped if there is no 1.8 satellite by the time the bt module looks.

/// Radio, visibility, auto-reconnect and codecs as they should be afterwards.
void request_config(bool radio, bool discoverable, bool auto_reconnect, uint8_t codecs);
/// A new device name, 1..32 bytes; the other settings stay as they are.
bool request_name(const char* name);
/// Forget one phone by address, or all of them with nullptr.
void request_forget(const uint8_t* addr);
/// Answer pairing request `seq`. The first answer to a request wins: a stale seq (the request
/// has moved on) or a request already answered — on the panel or in the web — is ignored, and
/// false says so.
bool request_pair_reply(uint32_t seq, bool accept, Source by = Source::PANEL);
/// Drop the phone that is connected (CMD_RADIO disconnect; works with a 1.7 satellite too).
void request_disconnect();
/// Ask for BT_CONFIG and BOND_LIST again.
void request_refresh();

/// The panel can confirm pairings: while true, the bt module announces HOST_CAP_PAIR_CONFIRM
/// and the satellite asks before letting a phone pair. Only a screen that is actually drawn
/// may set it — a headless build that set it would leave every pairing to time out.
void set_confirmer(bool on);
bool confirmer();

// ── the bt module's side ──

struct Pending {
    bool     config = false;
    bool     radio = false, discoverable = false, auto_reconnect = false;
    uint8_t  codecs = 0;
    bool     rename = false;
    char     name[NAME_CAP] = {};
    bool     forget = false;
    bool     forget_all = false;
    uint8_t  forget_addr[6] = {};
    bool     reply = false;
    uint32_t reply_seq = 0;
    bool     accept = false;
    bool     disconnect = false;
    bool     refresh = false;
};

/// Take every waiting request at once, leaving none.
bool take(Pending& out);

/// Writers for the bt module. Each bumps the revision.
void set_link(bool linked, bool settings);
void set_config(bool radio, bool discoverable, bool auto_reconnect, uint8_t supported,
                uint8_t enabled, const char* name, size_t name_len);
void set_bonds(uint8_t total, const Bond* bonds, uint8_t count);
/// A new pairing request (active), or a repeat of the current one (same addr/kind/passkey:
/// nothing changes, per the contract).
void pair_begin(const uint8_t addr[6], uint8_t kind, uint32_t passkey, const char* name, size_t name_len);
void pair_end(const uint8_t addr[6], uint8_t status);
/// The link went away: whatever was asked is not going to be answered.
void pair_abandon();

}  // namespace bt_settings
