#pragma once
#include "dlna_transport.h"
#include <string>
#include <string_view>

namespace dlna_core {
struct Element {
    std::string_view tag, attributes, body, whole;
};
inline std::string_view local_name(std::string_view name) {
    auto p = name.rfind(':');
    return p == name.npos ? name : name.substr(p + 1);
}
// A bounded XML scanner for device descriptions and DIDL. No entity declarations,
// external references, or recursive allocations. Tokens inside comments/CDATA are ignored.
inline bool element(std::string_view xml, std::string_view wanted, size_t &cursor, Element &out) {
    if (xml.find("<!DOCTYPE") != xml.npos || xml.find("<!ENTITY") != xml.npos)
        return false;
    std::string_view names[32];
    size_t depth = 0, first = 0, body = 0;
    Element candidate{};
    size_t p = cursor;
    while ((p = xml.find('<', p)) != xml.npos) {
        size_t start = p++;
        if (xml.substr(start, 4) == "<!--" || xml.substr(start, 9) == "<![CDATA[") {
            bool comment = xml.substr(start, 4) == "<!--";
            auto end = xml.find(comment ? "-->" : "]]>", p);
            if (end == xml.npos)
                return false;
            p = end + 3;
            continue;
        }
        if (p >= xml.size())
            return false;
        bool closing = xml[p] == '/';
        if (closing)
            ++p;
        size_t begin = p;
        while (p < xml.size() && xml[p] != '>' && xml[p] != '/' && xml[p] > ' ')
            ++p;
        auto name = xml.substr(begin, p - begin);
        size_t attrs = p;
        char quote = 0;
        for (; p < xml.size(); ++p) {
            char c = xml[p];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '>')
                break;
        }
        if (p == xml.size())
            return false;
        bool empty = p > start && xml[p - 1] == '/';
        ++p;
        if (name.empty() || name[0] == '?' || name[0] == '!')
            continue;
        if (!depth) {
            if (closing || local_name(name) != wanted)
                continue;
            candidate.tag = name;
            candidate.attributes = xml.substr(attrs, p - attrs - (empty ? 2 : 1));
            first = start;
            body = p;
            if (empty) {
                candidate.body = {};
                candidate.whole = xml.substr(first, p - first);
                out = candidate;
                cursor = p;
                return true;
            }
            names[depth++] = name;
        } else if (closing) {
            if (names[depth - 1] != name)
                return false;
            if (!--depth) {
                candidate.body = xml.substr(body, start - body);
                candidate.whole = xml.substr(first, p - first);
                out = candidate;
                cursor = p;
                return true;
            }
        } else if (!empty) {
            if (depth == 32)
                return false;
            names[depth++] = name;
        }
    }
    return false;
}
inline bool attribute(std::string_view a, std::string_view wanted, char *out, size_t cap) {
    out[0] = 0;
    size_t p = 0;
    while (p < a.size()) {
        while (p < a.size() && a[p] <= ' ')
            ++p;
        size_t first = p;
        while (p < a.size() && a[p] != '=' && a[p] > ' ')
            ++p;
        auto name = a.substr(first, p - first);
        while (p < a.size() && a[p] <= ' ')
            ++p;
        if (p == a.size() || a[p++] != '=')
            return false;
        while (p < a.size() && a[p] <= ' ')
            ++p;
        if (p == a.size() || (a[p] != '"' && a[p] != '\''))
            return false;
        char quote = a[p++];
        first = p;
        p = a.find(quote, p);
        if (p == a.npos)
            return false;
        if (local_name(name) == wanted)
            return decode_text(a.substr(first, p - first), out, cap);
        ++p;
    }
    return false;
}
inline bool http_url(std::string_view s) {
    if (s.find_first_of("\r\n\t ") != s.npos)
        return false;
    size_t start = s.substr(0, 7) == "http://" ? 7 : s.substr(0, 8) == "https://" ? 8 : 0;
    return start && s.size() > start && s[start] != '/' && s[start] != '?' && s[start] != '#';
}
inline bool resolve_url(std::string_view base, std::string_view value, char *out, size_t cap) {
    std::string url;
    if (http_url(value))
        url.assign(value);
    else {
        auto scheme = base.find("://");
        if (scheme == base.npos || value.empty())
            return false;
        auto origin_end = base.find('/', scheme + 3);
        auto origin = base.substr(0, origin_end);
        if (value.substr(0, 2) == "//")
            url = std::string(base.substr(0, scheme + 1)) + std::string(value);
        else if (value[0] == '/')
            url = std::string(origin) + std::string(value);
        else {
            auto end = base.find_first_of("?#");
            auto path = base.substr(0, end);
            auto slash = path.rfind('/');
            url = std::string(slash < scheme + 3 ? origin : path.substr(0, slash)) + "/" +
                  std::string(value);
        }
    }
    auto scheme = url.find("://");
    auto path = url.find('/', scheme + 3);
    // Normalize dot segments without touching query strings.
    if (path != std::string::npos) {
        auto q = url.find_first_of("?#", path);
        std::string suffix = q == std::string::npos ? "" : url.substr(q);
        std::string tail = url.substr(path, q == std::string::npos ? q : q - path), normalized;
        size_t p = 1;
        while (p <= tail.size()) {
            auto e = tail.find('/', p);
            if (e == std::string::npos)
                e = tail.size();
            auto part = tail.substr(p, e - p);
            if (part == "..") {
                auto back = normalized.rfind('/');
                if (back != std::string::npos)
                    normalized.resize(back);
            } else if (part != ".")
                normalized += "/" + part;
            if (e == tail.size())
                break;
            p = e + 1;
        }
        url.resize(path);
        url += normalized.empty() ? "/" : normalized;
        url += suffix;
    }
    if (!http_url(url) || url.size() >= cap || url.find_first_of("\r\n") != std::string::npos)
        return false;
    memcpy(out, url.c_str(), url.size() + 1);
    return true;
}
inline bool playable(std::string_view protocol) {
    if (protocol.substr(0, 11) != "http-get:*:")
        return false;
    auto mime = protocol.substr(11);
    mime = mime.substr(0, mime.find(':'));
    // Match the P4 decoder build and ConnectionManager sink capabilities.
    return mime == "audio/mpeg" || mime == "audio/mp3" || mime == "audio/x-mpeg" ||
           mime == "audio/mpeg3" || mime == "audio/flac" || mime == "audio/x-flac" ||
           mime == "audio/wav" || mime == "audio/x-wav" || mime == "audio/wave";
}
inline bool resource_metadata(Track &track, const char *protocol) {
    std::string result = "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
                         "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                         "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"><item id=\"0\" "
                         "parentID=\"0\" restricted=\"1\">";
    auto escape = [&](const char *text) {
        for (const char *p = text; *p; ++p) {
            switch (*p) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            default:
                result += *p;
            }
        }
    };
    auto field = [&](const char *name, const char *text) {
        result += "<";
        result += name;
        result += ">";
        escape(text);
        result += "</";
        result += name;
        result += ">";
    };
    field("dc:title", track.title);
    field("upnp:class", "object.item.audioItem.musicTrack");
    field("upnp:artist", track.artist);
    field("upnp:album", track.album);
    if (track.art[0])
        field("upnp:albumArtURI", track.art);
    result += "<res protocolInfo=\"";
    escape(protocol);
    result += "\">";
    escape(track.uri);
    result += "</res></item></DIDL-Lite>";
    if (!copy(track.metadata, result)) {
        track.metadata[0] = 0;
        return false;
    }
    return true;
}
} // namespace dlna_core
