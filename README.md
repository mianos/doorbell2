# doorbell3

ESP-IDF (v6, C++) firmware for an ESP32-C3 doorbell box. It:

- Plays MP3 chimes streamed from an HTTP URL on MQTT command — **queued**, so a
  doorbell press is never dropped while an hourly/camera chime is still playing
  (the core fix over the old Arduino `doorbell2`, which played one tone at a time).
- Reports room presence/tracking from a HiLink **LD2450** 24GHz mmWave radar over MQTT.
- Persists settings in NVS, keeps NTP time, and exposes an HTTP control surface
  (health, settings, and **OTA firmware update** with rollback).

This replaces the PlatformIO/Arduino `doorbell2` app. Audio runs in its own FreeRTOS
task fed by a bounded queue, fully decoupled from MQTT and radar.

## Hardware (Seeed XIAO ESP32-C3)

| Function | Signal | XIAO pin | GPIO |
|---|---|---|---|
| I2S amp (MAX98357A) | DOUT (data) | D2 | 4 |
| | BCLK | D1 | 3 |
| | LRCLK/WS | D0 | 2 |
| LD2450 radar | ESP RX (radar TX) | D7 | 20 |
| | ESP TX (radar RX) | D6 | 21 |

Radar UART is `UART_NUM_1` @ 256000 8N1. Pins are defaults in
`AudioPlayerConfig` (mianesp `audioplayer`) and the `Ld2450` constructor
([components/radar/include/Ld2450.h](components/radar/include/Ld2450.h)).

## Build / flash

```sh
. $IDF_PATH/export.sh        # ESP-IDF v6.0.x
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

First build fetches managed components: the shared
[mianesp](https://github.com/mianos/mianesp) components (see
`main/idf_component.yml`) plus `chmorgan/esp-libhelix-mp3` (Helix fixed-point
MP3 decoder), `espressif/mqtt`, `espressif/cjson`,
`espressif/network_provisioning`.

`build.sh` is a convenience wrapper that sources IDF and builds.

The flash layout is **dual-OTA** ([partitions.csv](partitions.csv)): `nvs` keeps its
standard offset/size so Wi-Fi creds and settings survive, `otadata` sits at
`0xf000`, and two 1.875 MB app slots (`ota_0`/`ota_1`) hold the running and
next image. Upgrading a device that was previously running the old single-`factory`
layout requires a full wired flash once (`idf.py flash` writes all partitions);
after that, updates go over the air. If you change `sdkconfig.defaults`, delete the
generated `sdkconfig` (or `idf.py fullclean`) so the new defaults are re-applied.

## OTA updates

Over-the-air update is HTTP-push: stream a raw `.bin` to the device and it writes
the inactive slot, flips the boot partition, and reboots.

```sh
idf.py build
curl --data-binary @build/doorbell3.bin http://<host-or-ip>/firmware
```

The new image boots in `PENDING_VERIFY`. A small `ota_verify` task waits (up to
120 s) for the device to get an IP, then calls
`esp_ota_mark_app_valid_cancel_rollback()`. If it never gets online it calls
`esp_ota_mark_app_invalid_rollback_and_reboot()` and the bootloader **rolls back**
to the previous slot (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). A panic or
watchdog reset before verification rolls back the same way. The check only runs
for a genuinely OTA'd image — a wired first-flash boots `UNDEFINED` and is left
alone, so a bench device with no Wi-Fi won't reboot-loop.

## HTTP interface (port 80)

| Method/Path | Body | Effect |
|---|---|---|
| `GET /healthz` | — | uptime, time, running `version`, `partition`, `heap_free` |
| `POST /firmware` | raw `.bin` | OTA: write inactive slot, set boot, reboot |
| `GET /firmware` | — | running `version`, `idf_ver`, build `date`/`time`, `partition` |
| `GET /config` | — | current settings as JSON |
| `POST /config` | any subset of settings keys | update + persist settings |
| `POST /config/reset` | `{"wifi":true}` (optional) | restore default settings; `wifi:true` also clears creds + reboots |
| `POST /reset` | — | clear Wi-Fi creds + reboot (provided by the base server) |
| `POST /set_hostname` | `{"host_name":"..."}` | set the Wi-Fi hostname |

`volume` changes via `POST /config` apply immediately; `sensor_name`, `mqtt_*`
and `tz` take effect on the next reboot (they re-derive topics + hostname).

## Wi-Fi provisioning

Provisioning uses the `wifimanager` component (**ESP-Touch v2 / SmartConfig**),
not the SoftAP flow of the old app — use the Espressif EspTouch app on first boot.
`POST /reset` (or `cmnd/<name>/reprovision`) clears credentials and reboots.

## MQTT interface

Anonymous, plain `mqtt://<server>:<port>` (default `mqtt2.mianos.com:1883`), with
a Last-Will published to the status topic. Topic base uses `sensor_name`
(default `doorbell3`).

Commands in — `cmnd/<name>/<cmd>` (payload must be non-empty JSON):

| Command | Payload | Effect |
|---|---|---|
| `play` | `{"url":"http://.../x.mp3","volume":80}` | Queue a chime (volume 0–100 optional) |
| `say` | `{"text":"hello","voice":"af_heart","volume":80}` | Speak via the FastKoko TTS server (`tts_url`); voice/volume optional. Also `POST /say` |
| `settings` | any subset of the settings keys below | Update + persist settings |
| `restart` | `{"x":1}` | `esp_restart()` |
| `reprovision` | `{"x":1}` | Clear Wi-Fi creds + reboot |

Telemetry out — `tele/<name>/...`:

- `init` — `{version, time, hostname, ip}` on connect
- `presence` — `{entry, time, ...target}` on detect/clear + every `presence` ms
- `tracking` — `{time, ...target}` every `tracking` ms (0 = off)
- `status` — `{time, uptime, heap_free, heap_min_free}` every 60 s (also the LWT topic)

## Settings (stored as JSON in NVS, defaults in [Settings.h](components/appsettings/include/Settings.h))

`mqtt_server`, `mqtt_port`, `sensor_name`, `tracking`, `presence`,
`detection_timeout`, `tz`, `volume`, `tts_url`, `tts_voice`. Missing/garbage
config falls back to defaults.

## Tests

Host-side unit tests for the LD2450 frame decoder (no hardware/IDF needed):

```sh
bash test/host/run.sh
```

## Components

Shared components (`wifimanager`, `webserver`, `jsonwrapper`,
`nvsstoragemanager`, `mqttwrapper`, `audioplayer`) are pulled from
[mianesp](https://github.com/mianos/mianesp) by the component manager.
App-local: `appsettings`, `radar`.
