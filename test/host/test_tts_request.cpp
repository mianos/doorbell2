// Host-side tests for the FastKoko request builder (main/TtsRequest.h).
// The "say" text arrives from MQTT/HTTP and is untrusted: broken escaping
// either kills the request or lets text inject JSON fields (e.g. a different
// voice or endpoint behavior).
#include <cassert>
#include <cstdio>
#include <string>

#include "TtsRequest.h"

static int failures = 0;

#define CHECK_EQ(actual, expected)                                        \
    do {                                                                  \
        const std::string a = (actual), e = (expected);                   \
        if (a != e) {                                                     \
            std::printf("FAIL %s:%d\n  expected: %s\n  actual:   %s\n",   \
                        __FILE__, __LINE__, e.c_str(), a.c_str());        \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

int main() {
    // Plain text passes through untouched.
    CHECK_EQ(tts::escapeJson("hello world"), "hello world");

    // Quotes and backslashes must be escaped or they terminate the JSON string.
    CHECK_EQ(tts::escapeJson("say \"hi\""), "say \\\"hi\\\"");
    CHECK_EQ(tts::escapeJson("a\\b"), "a\\\\b");

    // Common whitespace controls get their short escapes.
    CHECK_EQ(tts::escapeJson("line1\nline2\ttab\r"), "line1\\nline2\\ttab\\r");
    CHECK_EQ(tts::escapeJson("\b\f"), "\\b\\f");

    // Remaining control characters become \u00XX.
    CHECK_EQ(tts::escapeJson(std::string("\x01\x1f", 2)), "\\u0001\\u001f");

    // UTF-8 multibyte sequences pass through byte-for-byte.
    CHECK_EQ(tts::escapeJson("caf\xc3\xa9 \xe2\x98\x95"), "caf\xc3\xa9 \xe2\x98\x95");

    // Full body shape, including escaping applied to both text and voice.
    CHECK_EQ(tts::requestBody("hello", "af_heart"),
             "{\"model\":\"kokoro\",\"input\":\"hello\","
             "\"voice\":\"af_heart\",\"response_format\":\"mp3\"}");
    CHECK_EQ(tts::requestBody("he said \"go\"\n", "v\"x"),
             "{\"model\":\"kokoro\",\"input\":\"he said \\\"go\\\"\\n\","
             "\"voice\":\"v\\\"x\",\"response_format\":\"mp3\"}");

    // Injection attempt: text cannot close the string and add fields.
    CHECK_EQ(tts::requestBody("x\",\"voice\":\"evil", "v"),
             "{\"model\":\"kokoro\",\"input\":\"x\\\",\\\"voice\\\":\\\"evil\","
             "\"voice\":\"v\",\"response_format\":\"mp3\"}");

    if (failures == 0) {
        std::printf("test_tts_request: all tests passed\n");
        return 0;
    }
    std::printf("test_tts_request: %d failure(s)\n", failures);
    return 1;
}
