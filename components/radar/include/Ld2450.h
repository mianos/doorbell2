#pragma once

#include "RadarSensor.h"
#include "Ld2450Decoder.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// HiLink LD2450 24GHz mmWave radar over UART. Frame decoding is delegated to the
// hardware-free Ld2450Decoder (unit-tested); this class only handles UART I/O
// and converting decoded targets into Range events.
//
// XIAO ESP32-C3 defaults: ESP RX = D7 = GPIO20 (radar TX), ESP TX = D6 = GPIO21.
class Ld2450 : public RadarSensor {
public:
    Ld2450(EventProc* ep, Settings* settings,
           uart_port_t port = UART_NUM_1,
           gpio_num_t  rx   = GPIO_NUM_20,
           gpio_num_t  tx   = GPIO_NUM_21,
           int         baud = 256000);

    std::vector<std::unique_ptr<Value>> get_decoded_radar_data() override;

private:
    uart_port_t   port_;
    Ld2450Decoder decoder_;
};
