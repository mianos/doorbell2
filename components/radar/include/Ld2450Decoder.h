#pragma once

#include <cstdint>
#include <vector>

// One decoded LD2450 target. Plain data, no IDF/JSON dependencies, so the frame
// decoder can be unit-tested on the host.
struct Ld2450Target {
    float x;          // metres
    float y;          // metres
    float speed;      // m/s
    int   reference;  // target index 0..2
};

// Byte-oriented LD2450 frame decoder, extracted verbatim from the doorbell2
// state machine. Feed bytes one at a time; when a full frame completes it
// appends any non-zero targets to `out` and returns true.
class Ld2450Decoder {
public:
    bool feed(uint8_t byteValue, std::vector<Ld2450Target>& out);

    static int16_t decodeCoordinate(uint8_t lowByte, uint8_t highByte);
    static int16_t decodeSpeed(uint8_t lowByte, uint8_t highByte);

private:
    enum State { SEARCH_FOR_START, PROCESSING_DATA, VERIFY_END };
    static constexpr int TOTAL_TARGET_BYTES = 24;
    static constexpr int TOTAL_END_BYTES    = 2;

    bool isAllDistancesZero() const;

    State   state_         = SEARCH_FOR_START;
    int     startSeqCount_ = 0;
    int     dataByteCount_ = 0;
    int     endSeqCount_   = 0;
    uint8_t targetData_[TOTAL_TARGET_BYTES] = {};
};
