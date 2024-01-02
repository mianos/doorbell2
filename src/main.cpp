#include <ESPDateTime.h>
#include "AudioTools.h"
#include "AudioCodecs/CodecMP3Helix.h"

#include "provision.h"
#include "mqtt.h"
#include "lep.h"
#include "ld2450.h"

WiFiClient wifiClient;

URLStream url_streamer(4096);
I2SStream i2s;
VolumeStream volume(i2s);
MP3DecoderHelix decoder;
EncodedAudioStream dec(&volume, &decoder);  //dec(&i2s, &decoder); // Decoding stream

std::unique_ptr<StreamCopy> copier;
std::shared_ptr<RadarMqtt> mqtt;
std::shared_ptr<SettingsManager> settings;
RadarSensor *radarSensor;

unsigned long lastInvokeTime = 0; // Store the last time you called the function
const unsigned long dayMillis = 24UL * 60 * 60 * 1000; // Milliseconds in a day

void setup() {
  Serial.begin(115200);
  // esp_wifi_set_ps(WIFI_PS_NONE);
  wifi_connect();
  url_streamer.setClient(wifiClient);
  AudioLogger::instance().begin(Serial, AudioLogger::Error);
  auto cfg = i2s.defaultConfig(TX_MODE);
#if defined(SEEED)
  cfg.pin_data = D2;
  cfg.pin_bck = D1;
  cfg.pin_ws = D0;
#endif
  cfg.buffer_size = 1024;
  cfg.buffer_count = 10;
  cfg.channels = 1;
  cfg.sample_rate = 24000;

  i2s.begin(cfg);

  copier = std::make_unique<StreamCopy>(dec, url_streamer);
  volume.begin(cfg);

  settings = std::make_shared<SettingsManager>();
  volume.setVolume(settings->volume / 100.0);

  DateTime.setTimeZone(settings->tz.c_str());
  DateTime.begin(/* timeout param */);
  lastInvokeTime = millis();
  if (!DateTime.isTimeValid()) {
    Serial.printf("Failed to get time from server\n");
  }

  mqtt = std::make_shared<RadarMqtt>(std::move(copier), settings, dec, url_streamer, volume);
  auto *lep = new LocalEP{mqtt, settings};
  radarSensor = new LD2450{lep, settings};
}

void loop() {
  unsigned long currentMillis = millis();
  
  mqtt->handle();
  radarSensor->process();

  if (currentMillis - lastInvokeTime >= dayMillis) {
      DateTime.begin(1000);
      lastInvokeTime = currentMillis;
  }
}
