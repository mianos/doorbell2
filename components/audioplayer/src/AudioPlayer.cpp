#include "AudioPlayer.h"

#include <cstring>
#include <algorithm>

#include "esp_log.h"
#include "esp_http_client.h"
#include "mp3dec.h"   // Helix fixed-point MP3 decoder (chmorgan/esp-libhelix-mp3)

static const char* TAG = "audio";

// Helix layer-3 produces at most 1152 samples/channel/frame; stereo => 2304.
static constexpr int  kMaxSamples   = 2 * 1152;   // decoded interleaved int16
static constexpr int  kMaxStereo    = 2 * 1152;   // duplicated/clamped output
static constexpr int  kInBufSize    = 4096;       // > 2x max MP3 frame
static constexpr int  kMaxFrameBytes = 1441;      // largest possible MP3 frame
static constexpr int  kHttpChunk    = 1024;

AudioPlayer::AudioPlayer(const AudioPlayerConfig& cfg)
    : cfg_(cfg), default_volume_(cfg.default_volume) {}

AudioPlayer::~AudioPlayer() {
    if (task_) vTaskDelete(task_);
    if (tx_) {
        i2s_channel_disable(tx_);
        i2s_del_channel(tx_);
    }
    if (queue_) vQueueDelete(queue_);
    if (sbuf_) vStreamBufferDelete(sbuf_);
    if (reader_exited_) vSemaphoreDelete(reader_exited_);
}

void AudioPlayer::setDefaultVolume(int volume) {
    default_volume_.store(std::clamp(volume, 0, 100));
}

void AudioPlayer::start() {
    // --- I2S TX channel ---
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 8;
    chan_cfg.dma_frame_num = 480;     // 8x480 frames: ~160 ms at 24 kHz, ~87 ms at 44.1 kHz
    chan_cfg.auto_clear    = true;    // zero-fill on underrun (avoid DC buzz)
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg_.initial_sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = cfg_.bclk,
            .ws   = cfg_.ws,
            .dout = cfg_.dout,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_));
    cur_rate_ = cfg_.initial_sample_rate;

    // --- compressed-audio jitter buffer (reader task -> decoder) ---
    sbuf_ = xStreamBufferCreate(cfg_.stream_buf_size, 1);
    configASSERT(sbuf_);
    reader_exited_ = xSemaphoreCreateBinary();
    configASSERT(reader_exited_);

    // --- queue + consumer task ---
    queue_ = xQueueCreate(cfg_.queue_depth, sizeof(PlayRequest));
    configASSERT(queue_);
    xTaskCreate(taskTrampoline, "audio", cfg_.task_stack, this, cfg_.task_priority, &task_);
}

bool AudioPlayer::enqueue(const std::string& url, int volume) {
    if (url.empty() || url.size() >= sizeof(PlayRequest::url)) {
        ESP_LOGW(TAG, "rejecting url (empty or too long): %u bytes", (unsigned)url.size());
        return false;
    }
    PlayRequest req{};
    std::strncpy(req.url, url.c_str(), sizeof(req.url) - 1);
    req.volume = (volume < 0) ? -1 : (int16_t)std::clamp(volume, 0, 100);
    if (xQueueSend(queue_, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropping: %s", req.url);
        return false;
    }
    return true;
}

bool AudioPlayer::ensureSampleRate(uint32_t hz) {
    if (hz == cur_rate_ || hz == 0) return true;
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    ESP_ERROR_CHECK(i2s_channel_disable(tx_));
    esp_err_t err = i2s_channel_reconfig_std_clock(tx_, &clk);
    ESP_ERROR_CHECK(i2s_channel_enable(tx_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reconfig clock to %u failed: %s", (unsigned)hz, esp_err_to_name(err));
        return false;
    }
    cur_rate_ = hz;
    ESP_LOGI(TAG, "sample rate -> %u Hz", (unsigned)hz);
    return true;
}

void AudioPlayer::taskTrampoline(void* arg) {
    static_cast<AudioPlayer*>(arg)->run();
}

// Shared between playOne (decoder side) and the per-clip reader task. Lives on
// playOne's stack; playOne joins the reader (reader_exited_) before returning.
struct AudioPlayer::ReaderCtx {
    esp_http_client_handle_t http;
    StreamBufferHandle_t     sbuf;
    SemaphoreHandle_t        exited;
    std::atomic<bool>        abort{false};
    std::atomic<bool>        done{false};
};

// Streams the HTTP body into the jitter buffer until EOF, error, or abort.
// The decoder paces it: xStreamBufferSend blocks (bounded) when the buffer is
// full, so at most stream_buf_size of the clip is held in RAM at once.
void AudioPlayer::readerTask(void* arg) {
    auto* c = static_cast<ReaderCtx*>(arg);
    uint8_t chunk[kHttpChunk];
    while (!c->abort.load()) {
        int n = esp_http_client_read(c->http, (char*)chunk, sizeof(chunk));
        if (n < 0) { ESP_LOGE(TAG, "http read error"); break; }
        if (n == 0) break;  // EOF
        int off = 0;
        while (off < n && !c->abort.load()) {
            off += (int)xStreamBufferSend(c->sbuf, chunk + off, (size_t)(n - off),
                                          pdMS_TO_TICKS(100));
        }
    }
    c->done.store(true);
    xSemaphoreGive(c->exited);
    vTaskDelete(nullptr);
}

void AudioPlayer::run() {
    PlayRequest req;
    for (;;) {
        if (xQueueReceive(queue_, &req, portMAX_DELAY) == pdTRUE) {
            playOne(req);
        }
    }
}

void AudioPlayer::playOne(const PlayRequest& req) {
    const int vol = (req.volume < 0) ? default_volume_.load() : req.volume;
    const int32_t vol_q15 = (int32_t)vol * 32768 / 100;

    ESP_LOGI(TAG, "play (vol %d): %s", vol, req.url);

    // --- open HTTP stream ---
    esp_http_client_config_t hcfg = {};
    hcfg.url        = req.url;
    hcfg.timeout_ms = cfg_.http_timeout_ms;
    hcfg.buffer_size = kHttpChunk;
    esp_http_client_handle_t http = esp_http_client_init(&hcfg);
    if (!http) { ESP_LOGE(TAG, "http init failed"); return; }

    bool ok = false;
    bool reader_started = false;
    HMP3Decoder dec = nullptr;
    ReaderCtx rctx;
    // Heap buffers (kept off the task stack).
    uint8_t*  inbuf = (uint8_t*)malloc(kInBufSize);
    short*    pcm   = (short*)malloc(kMaxSamples * sizeof(short));
    int16_t*  out   = (int16_t*)malloc(kMaxStereo * sizeof(int16_t));
    if (!inbuf || !pcm || !out) { ESP_LOGE(TAG, "oom"); goto cleanup; }

    if (esp_http_client_open(http, 0) != ESP_OK) {
        ESP_LOGE(TAG, "http open failed: %s", req.url);
        goto cleanup;
    }
    esp_http_client_fetch_headers(http);
    {
        int status = esp_http_client_get_status_code(http);
        if (status != 200) { ESP_LOGE(TAG, "http status %d", status); goto cleanup; }
    }

    dec = MP3InitDecoder();
    if (!dec) { ESP_LOGE(TAG, "MP3InitDecoder failed"); goto cleanup; }

    // Hand the connection to the reader task; from here the decoder consumes
    // only from sbuf_ and touches http again only after joining the reader.
    xStreamBufferReset(sbuf_);
    rctx.http   = http;
    rctx.sbuf   = sbuf_;
    rctx.exited = reader_exited_;
    if (xTaskCreate(readerTask, "audio_rd", 4096, &rctx, cfg_.task_priority, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "reader task create failed");
        goto cleanup;
    }
    reader_started = true;

    // Prebuffer so Wi-Fi jitter is absorbed up front, not mid-clip. Clamped to
    // the buffer size: a full buffer must satisfy the wait or it never ends.
    while ((int)xStreamBufferBytesAvailable(sbuf_) <
               std::min(cfg_.prebuffer_bytes, cfg_.stream_buf_size) &&
           !rctx.done.load()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    {
        int      fill = 0;                 // valid bytes in inbuf
        uint8_t* read = inbuf;             // current decode pointer within inbuf
        bool     eof  = false;
        size_t   bytes_no_frame = 0;       // junk guard

        for (;;) {
            // Compact remaining bytes to the front, then top up from the
            // jitter buffer: grab whatever is there without blocking, and
            // block only when we lack a full frame's worth. A Wi-Fi stall
            // therefore never prevents decoding data already buffered.
            if (read != inbuf && fill > 0) memmove(inbuf, read, fill);
            read = inbuf;

            while (fill < kInBufSize && !eof) {
                TickType_t wait = (fill >= kMaxFrameBytes) ? 0 : pdMS_TO_TICKS(50);
                size_t got = xStreamBufferReceive(sbuf_, inbuf + fill,
                                                  (size_t)(kInBufSize - fill), wait);
                if (got > 0) { fill += (int)got; continue; }
                if (rctx.done.load() && xStreamBufferIsEmpty(sbuf_)) eof = true;
                else if (fill >= kMaxFrameBytes) break;  // decode what we have
                // else starved mid-clip: keep waiting for the reader
            }
            if (fill == 0) break;  // nothing left

            int offset = MP3FindSyncWord(read, fill);
            if (offset < 0) {
                // No frame sync in buffer; discard (keep tail in case of split sync).
                bytes_no_frame += fill;
                fill = 0;
                if (eof) break;
                if (bytes_no_frame > 64 * 1024) { ESP_LOGE(TAG, "no MP3 frames (junk?)"); break; }
                continue;
            }
            read += offset;
            fill -= offset;

            // Need a whole frame's worth before decoding; if short and not EOF, refill.
            if (fill < kMaxFrameBytes && !eof) {
                if (read != inbuf) memmove(inbuf, read, fill);
                read = inbuf;
                continue;
            }

            int err = MP3Decode(dec, &read, &fill, pcm, 0);
            if (err == ERR_MP3_NONE) {
                bytes_no_frame = 0;
                MP3FrameInfo fi;
                MP3GetLastFrameInfo(dec, &fi);
                ensureSampleRate((uint32_t)fi.samprate);

                int frames = (fi.nChans > 0) ? fi.outputSamps / fi.nChans : 0;
                if (frames > 1152) frames = 1152;
                int oi = 0;
                for (int i = 0; i < frames; ++i) {
                    int32_t s = (fi.nChans == 1) ? pcm[i] : pcm[2 * i];  // mono or L
                    s = (s * vol_q15) >> 15;
                    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
                    out[oi++] = (int16_t)s;  // L
                    out[oi++] = (int16_t)s;  // R (duplicate)
                }
                size_t written = 0;
                i2s_channel_write(tx_, out, oi * sizeof(int16_t), &written, portMAX_DELAY);
            } else if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
                if (eof) break;       // genuinely out of data
                // else loop refills
            } else {
                // Skip one byte and resync on other errors.
                if (fill > 0) { read++; fill--; }
                if (fill == 0 && eof) break;
            }
        }
        ok = true;
    }

cleanup:
    if (reader_started) {
        // Join the reader before closing http (it owns the connection until it
        // exits). Bounded: send blocks <=100 ms per round, read <= http timeout.
        rctx.abort.store(true);
        xSemaphoreTake(reader_exited_, portMAX_DELAY);
    }
    if (dec)   MP3FreeDecoder(dec);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    free(inbuf); free(pcm); free(out);
    ESP_LOGI(TAG, "play %s: %s", ok ? "done" : "aborted", req.url);
}
