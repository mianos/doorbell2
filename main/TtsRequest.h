#pragma once

#include <string>

// Builds the JSON body for FastKoko's (Kokoro-FastAPI) OpenAI-compatible
// POST /v1/audio/speech endpoint.
//
// Header-only and free of ESP-IDF dependencies: the input text arrives from
// MQTT and is untrusted, so the escaping is host-tested (test/host).
namespace tts {

inline std::string escapeJson(const std::string& in) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xf];
                    out += hex[c & 0xf];
                } else {
                    out += (char)c;  // UTF-8 bytes pass through
                }
        }
    }
    return out;
}

inline std::string requestBody(const std::string& text, const std::string& voice) {
    return "{\"model\":\"kokoro\",\"input\":\"" + escapeJson(text) +
           "\",\"voice\":\"" + escapeJson(voice) +
           "\",\"response_format\":\"mp3\"}";
}

}  // namespace tts
