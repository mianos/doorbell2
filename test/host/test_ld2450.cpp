// Host unit test for the LD2450 frame decoder. Pure C++ (no IDF), build & run
// with test/host/run.sh. This protects the trickiest ported logic: the binary
// frame state machine and the sign-bit coordinate/speed decoding.

#include "Ld2450Decoder.h"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>
#include <array>
#include <cstdint>

namespace {

int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

bool nearly(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// LD2450 little-endian sign-magnitude: bit15 set => positive, raw is low 15 bits.
void putCoord(std::vector<uint8_t>& f, int16_t mm) {
    uint16_t mag = (uint16_t)(mm < 0 ? -mm : mm);
    uint16_t raw = (mm >= 0) ? (mag | 0x8000) : mag;
    f.push_back(raw & 0xFF);
    f.push_back((raw >> 8) & 0xFF);
}

// Build one full frame for up to 3 targets {x_mm, y_mm, speed_cms}.
std::vector<uint8_t> makeFrame(std::vector<std::array<int16_t, 3>> targets) {
    std::vector<uint8_t> f = {0xAA, 0xFF, 0x03, 0x00};
    for (int i = 0; i < 3; ++i) {
        if (i < (int)targets.size()) {
            putCoord(f, targets[i][0]);  // x
            putCoord(f, targets[i][1]);  // y
            putCoord(f, targets[i][2]);  // speed
            f.push_back(0); f.push_back(0);  // resolution
        } else {
            for (int b = 0; b < 8; ++b) f.push_back(0);
        }
    }
    f.push_back(0x55); f.push_back(0xCC);
    return f;
}

std::vector<Ld2450Target> decodeAll(const std::vector<uint8_t>& bytes) {
    Ld2450Decoder dec;
    std::vector<Ld2450Target> out;
    for (uint8_t b : bytes) dec.feed(b, out);
    return out;
}

void test_single_target() {
    auto f = makeFrame({{{1000, 2000, 100}}});       // 1.0m, 2.0m, 1.0 m/s
    auto out = decodeAll(f);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(nearly(out[0].x, 1.0f));
        CHECK(nearly(out[0].y, 2.0f));
        CHECK(nearly(out[0].speed, 100 * 0.036f));
        CHECK(out[0].reference == 0);
    }
}

void test_negative_coords() {
    auto f = makeFrame({{{-1500, -500, -50}}});
    auto out = decodeAll(f);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        CHECK(nearly(out[0].x, -1.5f));
        CHECK(nearly(out[0].y, -0.5f));
        CHECK(nearly(out[0].speed, -50 * 0.036f));
    }
}

void test_all_zero_frame_yields_nothing() {
    auto f = makeFrame({});                          // all targets zero
    auto out = decodeAll(f);
    CHECK(out.empty());
}

void test_target_with_zero_x_skipped() {
    // Original logic only emits a target when x != 0.
    auto f = makeFrame({{{0, 2000, 100}}, {{1200, 0, 0}}});
    auto out = decodeAll(f);
    CHECK(out.size() == 1);
    if (out.size() == 1) CHECK(out[0].reference == 1);
}

void test_resync_after_garbage() {
    std::vector<uint8_t> bytes = {0x12, 0x34, 0xAA, 0x00, 0xFF};  // false starts
    auto f = makeFrame({{{800, 800, 20}}});
    bytes.insert(bytes.end(), f.begin(), f.end());
    auto out = decodeAll(bytes);
    CHECK(out.size() == 1);
    if (out.size() == 1) CHECK(nearly(out[0].x, 0.8f));
}

void test_frame_split_across_feeds() {
    // Feeding byte-by-byte (as from a UART) must still assemble the frame.
    auto f = makeFrame({{{1000, 1000, 0}}});
    Ld2450Decoder dec;
    std::vector<Ld2450Target> out;
    bool completed = false;
    for (uint8_t b : f) completed |= dec.feed(b, out);
    CHECK(completed);
    CHECK(out.size() == 1);
}

}  // namespace

int main() {
    test_single_target();
    test_negative_coords();
    test_all_zero_frame_yields_nothing();
    test_target_with_zero_x_skipped();
    test_resync_after_garbage();
    test_frame_split_across_feeds();

    if (g_failures == 0) {
        printf("OK: all LD2450 decoder tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
