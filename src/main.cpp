#include "AudioTools.h"
#include "AudioCodecs/CodecMP3Helix.h"

#include "provision.h"
#include "mqtt.h"

WiFiClient wifiClient;

URLStream url_streamer("", "");

I2SStream i2s;
MP3DecoderHelix decoder;
EncodedAudioStream dec(&i2s, new MP3DecoderHelix()); // Decoding stream

std::unique_ptr<StreamCopy> copier;
std::unique_ptr<RadarMqtt> mqtt;


void setup() {
  Serial.begin(115200);
  wifi_connect();
  url_streamer.setClient(wifiClient);
  AudioLogger::instance().begin(Serial, AudioLogger::Error);

// pinMode(nextButtonPin, INPUT_PULLUP);

  // setup output
  auto cfg = i2s.defaultConfig(TX_MODE);
#if defined(SEEED)
  cfg.pin_data = D2;
  cfg.pin_bck = D1;
  cfg.pin_ws = D0;

#else
  cfg.pin_data = 27;
  cfg.pin_bck = 26;
  cfg.pin_ws = 25;
#endif
  i2s.begin(cfg);

  copier = std::make_unique<StreamCopy>(dec, url_streamer);
  // setup player
  dec.setNotifyAudioChange(i2s);
  dec.begin();

  //url_streamer.begin("http://stream.srg-ssr.ch/m/rsj/mp3_128","audio/mp3");

  mqtt = std::make_unique<RadarMqtt>(std::move(copier));
}

#if 0
// Sets the volume control from a linear potentiometer input
void updateVolume() {
  // Reading potentiometer value (range is 0 - 4095)
  float vol = static_cast<float>(analogRead(volumePin));
  // min in 0 - max is 1.0
  player.setVolume(vol/4095.0);
}

#endif

void loop() {
  //updateVolume(); // remove comments to activate volume control
  // updatePosition();  // remove comments to activate position control
  //player.copy();
  mqtt->handle();
}
