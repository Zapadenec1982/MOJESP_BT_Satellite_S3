#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

// Platform-independent bounded transport data. The caller serializes access.
namespace dlna_core {
constexpr size_t URI_CAP = 257;
constexpr size_t META_CAP = 2049;

inline bool time_seconds(std::string_view text, int32_t &seconds) {
    const auto dot = text.find('.');
    if (dot != text.npos) {
        const auto fraction = text.substr(dot + 1);
        if (fraction.empty())
            return false;
        for (char c : fraction)
            if (c < '0' || c > '9')
                return false;
        text = text.substr(0, dot);
    }
    uint32_t parts[3]{};
    size_t part = 0;
    bool digit = false;
    for (char c : text) {
        if (c == ':' && digit && part < 2) {
            ++part;
            digit = false;
            continue;
        }
        if (c < '0' || c > '9' || parts[part] > 596523u)
            return false;
        parts[part] = parts[part] * 10 + unsigned(c - '0');
        digit = true;
    }
    if (part != 2 || !digit || parts[1] >= 60 || parts[2] >= 60)
        return false;
    const uint64_t total = uint64_t(parts[0]) * 3600 + parts[1] * 60 + parts[2];
    if (total > INT32_MAX)
        return false;
    seconds = static_cast<int32_t>(total);
    return true;
}

template <size_t N> bool copy(char (&out)[N], std::string_view value) {
    if (value.size() >= N)
        return false;
    memmove(out, value.data(), value.size());
    out[value.size()] = 0;
    return true;
}

inline bool decode_text(std::string_view in, char *out, size_t cap) {
    if (!cap)
        return false;
    size_t n = 0;
    out[0] = 0;
    auto append = [&](uint32_t cp) {
        char bytes[4];
        size_t count = 0;
        if (cp < 0x80)
            bytes[count++] = static_cast<char>(cp);
        else if (cp < 0x800) {
            bytes[count++] = char(0xc0 | (cp >> 6));
            bytes[count++] = char(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            bytes[count++] = char(0xe0 | (cp >> 12));
            bytes[count++] = char(0x80 | ((cp >> 6) & 63));
            bytes[count++] = char(0x80 | (cp & 63));
        } else {
            bytes[count++] = char(0xf0 | (cp >> 18));
            bytes[count++] = char(0x80 | ((cp >> 12) & 63));
            bytes[count++] = char(0x80 | ((cp >> 6) & 63));
            bytes[count++] = char(0x80 | (cp & 63));
        }
        if (n + count >= cap)
            return false;
        memcpy(out + n, bytes, count);
        n += count;
        out[n] = 0;
        return true;
    };
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            if (n + 1 >= cap) {
                out[0] = 0;
                return false;
            }
            out[n++] = in[i];
            out[n] = 0;
            continue;
        }
        const auto end = in.find(';', i + 1);
        if (end == in.npos) {
            out[0] = 0;
            return false;
        }
        const auto e = in.substr(i + 1, end - i - 1);
        uint32_t cp = 0;
        if (e == "amp")
            cp = '&';
        else if (e == "lt")
            cp = '<';
        else if (e == "gt")
            cp = '>';
        else if (e == "quot")
            cp = '"';
        else if (e == "apos")
            cp = '\'';
        else if (e.size() > 1 && e[0] == '#') {
            size_t j = 1;
            unsigned base = 10;
            if (e[j] == 'x') {
                base = 16;
                ++j;
            }
            if (j == e.size()) {
                out[0] = 0;
                return false;
            }
            for (; j < e.size(); ++j) {
                const char c = e[j];
                unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                                 : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                        : 99;
                if (digit >= base || cp > (0x10ffffu - digit) / base) {
                    out[0] = 0;
                    return false;
                }
                cp = cp * base + digit;
            }
        } else {
            out[0] = 0;
            return false;
        }
        if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            (cp < 32 && cp != 9 && cp != 10 && cp != 13) || !append(cp)) {
            out[0] = 0;
            return false;
        }
        i = end;
    }
    return true;
}

// Reads simple DIDL text elements by local name (prefixes are sender-defined).
// No DTD/entity expansion, recursion, or allocation. Oversized fields are rejected.
inline bool field(std::string_view xml, std::string_view name, char *out, size_t cap) {
    if (!cap)
        return false;
    out[0] = 0;
    if (xml.find("<!DOCTYPE") != xml.npos || xml.find("<!ENTITY") != xml.npos)
        return false;
    size_t p = 0;
    while ((p = xml.find('<', p)) != xml.npos) {
        if (xml.substr(p, 4) == "<!--") {
            auto end = xml.find("-->", p + 4);
            if (end == xml.npos)
                return false;
            p = end + 3;
            continue;
        }
        if (xml.substr(p, 9) == "<![CDATA[") {
            auto end = xml.find("]]>", p + 9);
            if (end == xml.npos)
                return false;
            p = end + 3;
            continue;
        }
        size_t first = ++p;
        if (p >= xml.size())
            break;
        if (xml[p] == '/' || xml[p] == '?' || xml[p] == '!')
            continue;
        while (p < xml.size() && xml[p] != '>' && xml[p] != '/' && xml[p] != ' ' &&
               xml[p] != '\t' && xml[p] != '\r' && xml[p] != '\n')
            ++p;
        const auto qualified = xml.substr(first, p - first);
        const auto colon = qualified.rfind(':');
        const auto local = colon == qualified.npos ? qualified : qualified.substr(colon + 1);
        char quote = 0;
        while (p < xml.size()) {
            const char c = xml[p++];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '>')
                break;
        }
        if (p > xml.size() || p == 0 || xml[p - 1] != '>')
            return false;
        if (local != name)
            continue;
        if (p >= 2 && xml[p - 2] == '/')
            return true;
        if (xml.substr(p, 9) == "<![CDATA[") {
            const auto end = xml.find("]]>", p + 9);
            if (end == xml.npos || end - p - 9 >= cap)
                return false;
            memcpy(out, xml.data() + p + 9, end - p - 9);
            out[end - p - 9] = 0;
            return true;
        }
        const auto end = xml.find('<', p);
        return end != xml.npos && decode_text(xml.substr(p, end - p), out, cap);
    }
    return false;
}

struct Track {
    char uri[URI_CAP]{};
    char metadata[META_CAP]{};
    char title[96]{}, artist[96]{}, album[96]{}, art[URI_CAP]{};
    bool set_uri(std::string_view value) {
        if (value.size() >= sizeof(uri))
            return false;
        *this = Track{};
        return copy(uri, value);
    }
    bool set_metadata(std::string_view value) {
        if (!copy(metadata, value))
            return false;
        field(value, "title", title, sizeof(title));
        field(value, "artist", artist, sizeof(artist));
        if (!artist[0])
            field(value, "creator", artist, sizeof(artist));
        field(value, "album", album, sizeof(album));
        field(value, "albumArtURI", art, sizeof(art));
        return true;
    }
};

// The bundled UPnP formatter inserts callback output verbatim into XML.
// Return actual bytes written; never snprintf's would-have-written length.
inline int xml_reply(std::string_view text, char *out, size_t cap) {
    if (!out || !cap)
        return 0;
    size_t n = 0;
    out[0] = 0;
    for (char c : text) {
        const char *entity = c == '&'    ? "&amp;"
                             : c == '<'  ? "&lt;"
                             : c == '>'  ? "&gt;"
                             : c == '"'  ? "&quot;"
                             : c == '\'' ? "&apos;"
                                         : nullptr;
        const size_t len = entity ? strlen(entity) : 1;
        if (n + len >= cap) {
            out[0] = 0;
            return 0;
        }
        if (entity)
            memcpy(out + n, entity, len);
        else
            out[n] = c;
        n += len;
    }
    out[n] = 0;
    return static_cast<int>(n);
}

// A completion belongs to this renderer only when both ends of the handoff match.
inline bool pending_handoff(const char *active, const char *playing, const char *ended,
                            const char *promoted, bool unseen) {
    return unseen && active[0] && promoted[0] && !strcmp(active, ended) &&
           !strcmp(playing, promoted);
}

enum class Action { Play, Stop, Pause, Seek, Select, Adopt };
enum class Mode { Playing, Paused, Stopped };
struct Command {
    Action action = Action::Stop;
    int32_t seconds = 0;
    Track track{};
    Mode mode = Mode::Playing;
    bool restart = false;
};

class Transport {
  public:
    static constexpr size_t CAPACITY = 8;
    Track current{}, next{}, previous{};
    void clear_commands() {
        head_ = count_ = 0;
    }
    bool push(Action action, int32_t seconds = 0, Mode mode = Mode::Playing, bool restart = false) {
        if (count_ == CAPACITY || (action == Action::Play && !current.uri[0]))
            return false;
        auto &cmd = queue_[(head_ + count_) % CAPACITY];
        cmd.action = action;
        cmd.seconds = seconds;
        cmd.mode = mode;
        cmd.restart = restart;
        // Capture identity now: a later SetURI must not change an earlier Play.
        if (action == Action::Play || action == Action::Select || action == Action::Adopt)
            cmd.track = current;
        ++count_;
        return true;
    }
    bool pop(Command &cmd) {
        if (!count_)
            return false;
        cmd = queue_[head_];
        head_ = (head_ + 1) % CAPACITY;
        --count_;
        return true;
    }
    bool advance(Mode mode = Mode::Playing, bool prepared = false) {
        if (!next.uri[0] || count_ == CAPACITY)
            return false;
        previous = current;
        current = next;
        next = Track{};
        return push(mode == Mode::Playing ? (prepared ? Action::Adopt : Action::Play)
                                          : Action::Select, 0, mode, true);
    }
    bool back(Mode mode = Mode::Playing) {
        if (!previous.uri[0])
            return push(Action::Seek, 0);
        if (count_ == CAPACITY)
            return false;
        next = current;
        current = previous;
        previous = Track{};
        return push(mode == Mode::Playing ? Action::Play : Action::Select, 0, mode, true);
    }

  private:
    Command queue_[CAPACITY]{};
    size_t head_ = 0, count_ = 0;
};
} // namespace dlna_core
