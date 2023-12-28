#include "AudioTools.h"
#include "AudioCodecs/CodecMP3Helix.h"

#include "provision.h"
#include "mqtt.h"

const char *urls[] = {
  "http://stream.srg-ssr.ch/m/rsj/mp3_128",
  "http://stream.srg-ssr.ch/m/drs3/mp3_128",
  "http://stream.srg-ssr.ch/m/rr/mp3_128",
  "http://sunshineradio.ice.infomaniak.ch/sunshineradio-128.mp3",
  "http://streaming.swisstxt.ch/m/drsvirus/mp3_128"
};
const char *wifi = "iot";
const char *password = "iotlongpassword";

WiFiClient wifiClient;
// URLStream urlStream(wifi, password);
URLStream urlStream("", "");
AudioSourceURL source(urlStream, urls, "audio/mp3");
I2SStream i2s;
MP3DecoderHelix decoder;

std::unique_ptr<AudioPlayer> player;
std::unique_ptr<RadarMqtt> mqtt;

// additional controls
const int volumePin = A0;
Debouncer nextButtonDebouncer(2000);
const int nextButtonPin = 0;

void setup() {
  Serial.begin(115200);
  wifi_connect();
  urlStream.setClient(wifiClient);
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

  player = std::make_unique<AudioPlayer>(source, i2s, decoder);
  // setup player
  player->begin();
  mqtt = std::make_unique<RadarMqtt>(std::move(player));
}

#if 0
// Sets the volume control from a linear potentiometer input
void updateVolume() {
  // Reading potentiometer value (range is 0 - 4095)
  float vol = static_cast<float>(analogRead(volumePin));
  // min in 0 - max is 1.0
  player.setVolume(vol/4095.0);
}


// Moves to the next url when we touch the pin
void updatePosition() {
   if (digitalRead(nextButtonPin) == LOW) {
      Serial.println("Moving to next url");
      if (nextButtonDebouncer.debounce()){
        player.next();
      }
  }
}
#endif

void loop() {
  //updateVolume(); // remove comments to activate volume control
  // updatePosition();  // remove comments to activate position control
  //player.copy();
  mqtt->handle();
}
