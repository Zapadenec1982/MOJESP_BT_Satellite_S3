/**
 * @file playlist.h
 * @brief Every playable file on the card, and where the player is in them.
 *
 * A component rather than part of modules/player because two sides need it and a module may
 * not depend on another module: the player owns the cursor and moves it, and modules/ui_lvgl
 * draws the list.
 *
 * It used to hold one folder at a time, on the argument that the question a player asks is
 * "what else is in this folder". That was wrong about how people use a player: you choose
 * the card as a source and expect to see your music, not to go looking for it a directory
 * at a time. So this is an index of the whole card — one recursive read when the card
 * appears, and after that every question is answered from RAM.
 *
 * Each entry is a path relative to the root, so sorting by it keeps an album together and
 * in track order without any special case for folders.
 *
 * Concurrency: the writer is the player task, the readers are the LVGL and main-loop tasks,
 * and neither reader may block the player. A generation counter is enough — odd while the
 * list is being rebuilt, even when settled, and a reader that sees it move retries. The
 * cursor moves per track and is a plain atomic.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace player_core {

/// What happens when a track ends. Persisted, so the numbers are part of the contract.
enum class Repeat : uint8_t { OFF = 0, ONE = 1, ALL = 2 };

class Playlist {
public:
    /// A card holds more music than a folder did. At MAX_REL bytes each this is the largest
    /// index that still costs less PSRAM than one second of audio.
    static constexpr int MAX_TRACKS = 2048;
    /// "Artist/Album/01 - Title.flac" — the path under the root, not the whole thing.
    static constexpr int MAX_REL  = 192;
    static constexpr int MAX_NAME = 96;    ///< the file name alone, for display
    static constexpr int MAX_DIR  = 160;   ///< the root, and the folder of one entry

    /// Allocate the backing store (PSRAM). Safe to call more than once.
    bool init();

    // ── writer side: the player task only ──
    void begin(const char* root);         ///< start a rebuild; readers see "rebuilding"
    bool add(const char* rel);            ///< relative path; false when full
    void commit(int cursor);              ///< publish, and place the cursor
    /// Sort what has been added, by relative path, case-insensitively. Writer-side, valid
    /// between begin() and commit(). Here rather than in the caller because this is where
    /// the buffer is, and the buffer is far larger than the player task's stack.
    void sort_pending();
    /// Index of `rel` among what has been added, or −1. Writer-side.
    int  find_pending(const char* rel) const;
    int  pending() const { return fill_; }

    // ── reader side ──
    int      count() const  { return count_.load(std::memory_order_acquire); }
    int      cursor() const { return cursor_.load(std::memory_order_acquire); }
    uint32_t generation() const { return gen_.load(std::memory_order_acquire); }
    void     set_cursor(int i);

    /// The file name of entry `i`, without its folder — what a list row shows.
    bool name_at(int i, char* out, size_t n) const;
    /// The folder of entry `i` relative to the root, or "" at the top. The dim half of a row.
    bool folder_at(int i, char* out, size_t n) const;
    /// The path relative to the root, which is what identifies an entry across a rescan.
    bool rel_at(int i, char* out, size_t n) const;
    /// The full path, ready for fopen().
    bool path_at(int i, char* out, size_t n) const;
    /// Index of `rel` in the published list, or −1. Reader-side, for restoring a cursor.
    int  find(const char* rel) const;
    bool root(char* out, size_t n) const;

    /// Where the cursor goes when a track ends by itself. −1 means "stop".
    int  advance(bool shuffle, Repeat repeat) const;
    /// Where the cursor goes when the user asks for the next one. Never stops on ALL/ONE,
    /// and never repeats a single track: asking for "next" and getting the same track again
    /// is not what the button says.
    int  next(bool shuffle, Repeat repeat) const;
    int  previous(bool shuffle) const;

private:
    const char* at(int i) const { return rels_ + (size_t)i * MAX_REL; }
    char* at(int i) { return rels_ + (size_t)i * MAX_REL; }

    char* rels_ = nullptr;                ///< MAX_TRACKS * MAX_REL, in PSRAM
    char  root_[MAX_DIR] = "";
    std::atomic<uint32_t> gen_{0};
    std::atomic<int> count_{0};
    std::atomic<int> cursor_{0};
    int   fill_ = 0;                      ///< writer-private, valid between begin/commit
};

Playlist& playlist();

}  // namespace player_core
