#include "Ld2450.h"

Ld2450::Ld2450(EventProc* ep, Settings* settings,
               uart_port_t port, gpio_num_t rx, gpio_num_t tx, int baud)
    : RadarSensor(ep, settings), port_(port) {
    uart_config_t cfg = {};
    cfg.baud_rate  = baud;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(port_, 1024, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(port_, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(port_, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

std::vector<std::unique_ptr<Value>> Ld2450::get_decoded_radar_data() {
    std::vector<std::unique_ptr<Value>> valuesList;
    std::vector<Ld2450Target> targets;
    uint8_t byteValue;

    // Read one byte at a time (mirrors the original available()/read() loop),
    // returning as soon as a complete frame is decoded.
    while (uart_read_bytes(port_, &byteValue, 1, 0) == 1) {
        if (decoder_.feed(byteValue, targets)) {
            for (const auto& t : targets) {
                valuesList.push_back(std::make_unique<Range>(t.x, t.y, t.speed, t.reference));
            }
            return valuesList;
        }
    }
    return valuesList;
}
