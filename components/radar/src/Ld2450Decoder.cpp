#include "Ld2450Decoder.h"

int16_t Ld2450Decoder::decodeCoordinate(uint8_t lowByte, uint8_t highByte) {
    int16_t coordinate = (highByte & 0x7F) << 8 | lowByte;
    if ((highByte & 0x80) == 0) coordinate = -coordinate;
    return coordinate;
}

int16_t Ld2450Decoder::decodeSpeed(uint8_t lowByte, uint8_t highByte) {
    int16_t speed = (highByte & 0x7F) << 8 | lowByte;
    if ((highByte & 0x80) == 0) speed = -speed;
    return speed;
}

bool Ld2450Decoder::isAllDistancesZero() const {
    for (int i = 0; i < 3; i++) {
        int16_t x = decodeCoordinate(targetData_[i * 8],     targetData_[i * 8 + 1]);
        int16_t y = decodeCoordinate(targetData_[i * 8 + 2], targetData_[i * 8 + 3]);
        int16_t speed = decodeSpeed(targetData_[i * 8 + 4],  targetData_[i * 8 + 5]);
        if (x != 0 || y != 0 || speed != 0) return false;
    }
    return true;
}

bool Ld2450Decoder::feed(uint8_t byteValue, std::vector<Ld2450Target>& out) {
    switch (state_) {
        case SEARCH_FOR_START:
            if ((startSeqCount_ == 0 && byteValue == 0xAA) ||
                (startSeqCount_ == 1 && byteValue == 0xFF) ||
                (startSeqCount_ == 2 && byteValue == 0x03) ||
                (startSeqCount_ == 3 && byteValue == 0x00)) {
                startSeqCount_++;
            } else {
                startSeqCount_ = 0;
            }
            if (startSeqCount_ == 4) {
                state_         = PROCESSING_DATA;
                dataByteCount_ = 0;
                startSeqCount_ = 0;
            }
            break;

        case PROCESSING_DATA:
            targetData_[dataByteCount_++] = byteValue;
            if (dataByteCount_ == TOTAL_TARGET_BYTES) {
                state_ = VERIFY_END;
            }
            break;

        case VERIFY_END:
            if ((endSeqCount_ == 0 && byteValue == 0x55) ||
                (endSeqCount_ == 1 && byteValue == 0xCC)) {
                endSeqCount_++;
            } else {
                state_       = SEARCH_FOR_START;
                endSeqCount_ = 0;
            }
            if (endSeqCount_ == TOTAL_END_BYTES) {
                if (!isAllDistancesZero()) {
                    for (int i = 0; i < 3; i++) {
                        int16_t x = decodeCoordinate(targetData_[i * 8],     targetData_[i * 8 + 1]);
                        int16_t y = decodeCoordinate(targetData_[i * 8 + 2], targetData_[i * 8 + 3]);
                        int16_t speed = decodeSpeed(targetData_[i * 8 + 4],  targetData_[i * 8 + 5]);
                        if (x) {
                            out.push_back(Ld2450Target{
                                static_cast<float>(x) / 1000.0f,
                                static_cast<float>(y) / 1000.0f,
                                static_cast<float>(speed) * 0.036f,
                                i});
                        }
                    }
                }
                state_       = SEARCH_FOR_START;
                endSeqCount_ = 0;
                return true;
            }
            break;
    }
    return false;
}
