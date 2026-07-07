#pragma once

#include <string>
#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"

// AudioPlayer streams an MP3 from an HTTP URL, decodes it (Helix, fixed-point),
// and writes PCM to an I2S amplifier (e.g. MAX98357A).
//
// Concurrency model (the core fix vs the old Arduino app): play requests are
// pushed onto a bounded FreeRTOS queue and consumed by a single dedicated task,
// one after another. enqueue() never blocks the caller (MQTT/radar contexts);
// nothing is dropped except on queue overflow (logged).
//
// Per clip, a short-lived reader task streams HTTP into a stream buffer while
// the audio task decodes from it, so Wi-Fi stalls are absorbed by buffered
// compressed data (plus the I2S DMA cushion) instead of reaching the speaker.
// Playback starts only after prebuffer_bytes (or the whole clip) has arrived.
struct AudioPlayerConfig {
    // XIAO ESP32-C3 defaults: D2=GPIO4 (data), D1=GPIO3 (bclk), D0=GPIO2 (ws).
    gpio_num_t bclk = GPIO_NUM_3;
    gpio_num_t ws   = GPIO_NUM_2;
    gpio_num_t dout = GPIO_NUM_4;

    int      default_volume      = 100;    // 0..100, applied when a request omits volume
    int      queue_depth         = 4;      // bounded; overflow -> drop + log
    uint32_t initial_sample_rate = 24000;  // re-derived per stream from the MP3
    int      task_priority       = 5;      // below Wi-Fi/lwIP
    int      task_stack          = 6144;
    int      http_timeout_ms     = 8000;

    // Compressed-audio jitter buffer between the HTTP reader and the decoder.
    // 32 KB ~= 2 s at 128 kbps; prebuffer trades ~that much start latency for
    // immunity to Wi-Fi stalls shorter than the buffered span.
    int      stream_buf_size     = 32768;
    int      prebuffer_bytes     = 16384;
};

class AudioPlayer {
public:
    explicit AudioPlayer(const AudioPlayerConfig& cfg = {});
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&)            = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Create the I2S channel and the consumer task. Call once after construction.
    void start();

    // Queue a sound. volume is 0..100, or -1 to use the configured default.
    // Returns false (and logs) if the queue is full. Non-blocking.
    bool enqueue(const std::string& url, int volume = -1);

    void setDefaultVolume(int volume);  // 0..100

private:
    struct PlayRequest {
        char    url[256];
        int16_t volume;  // 0..100, or -1 for default
    };

    struct ReaderCtx;

    static void taskTrampoline(void* arg);
    static void readerTask(void* arg);
    void run();
    void playOne(const PlayRequest& req);
    bool ensureSampleRate(uint32_t hz);

    AudioPlayerConfig   cfg_;
    QueueHandle_t       queue_ = nullptr;
    TaskHandle_t        task_  = nullptr;
    i2s_chan_handle_t   tx_    = nullptr;
    StreamBufferHandle_t sbuf_ = nullptr;
    SemaphoreHandle_t   reader_exited_ = nullptr;
    uint32_t            cur_rate_ = 0;
    std::atomic<int>    default_volume_;
};
