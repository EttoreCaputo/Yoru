# Yoru

![Target](https://img.shields.io/badge/target-ESP32--S3-brightgreen)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.1-blue)
![LVGL](https://img.shields.io/badge/LVGL-9.3.0-orange)
![License](https://img.shields.io/badge/license-MIT-green)

A desk companion that lives in the black of a round AMOLED: a small matte sphere with two tiny eyes and two floating hands, in the dark, that you can make your own. It is a pet that reacts to you and to your computer, and an AI assistant you can talk to, running on your own computer's models or an API of your choice.

Status: early development (firmware 0.3.0, milestone M3: voice and link).

- [The character](docs/CONCEPT.md)
- [Roadmap and hardware](docs/ROADMAP.md)
- [Protocol](docs/protocol.md): what differs from DualEye's

## Host program

On the computer, Yoru is driven by [Companion Hub](https://github.com/EttoreCaputo/companion-hub), the desktop app and CLI it shares with DualEye. It runs the voice assistant (local or API models), sends the computer's state for the pet's mood, flashes and updates the firmware, pairs Yoru over WiFi and lets Claude use it over MCP. It is meant to drive several boards at once. Yoru support there is in progress (milestone M4).

The app carries this firmware: after a change, build it here and run `tools/fetch_firmware.sh` in Companion Hub.

## Hardware

[Waveshare ESP32-S3-Touch-AMOLED-1.75C](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C): ESP32-S3R8, 1.75" 466 × 466 AMOLED with touch, ES8311 + ES7210 audio with two mics, AXP2101 battery management, QMI8658 IMU.

## Build

ESP-IDF 6.1:

```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py build merge-bin
idf.py -p /dev/cu.usbmodem* flash
```

If the board doesn't show up as a serial port, hold BOOT while plugging it in.

Once it boots, the board talks protocol v2 on the USB port, so `idf.py monitor` only shows the boot messages and then frames. To read its logs and call its tools without Companion Hub, use the test scripts (pyserial, in the ESP-IDF Python env):

```bash
python3 tools/audio_selftest.py tone
```

`tools/yoru_link.py` is the small library they share: `Link().hello()`, `call("tools/call", {"name": "get_state", "arguments": {}})`, and `frames()` for the log lines.

Say "Alexa" (or "Hi ESP", with `set_wake_word`) and Yoru listens: its eyes open wide and look at you. Without a host nobody answers yet: if you said something it thinks for up to 30 s, then goes back to its own life.

## Credits

Pins and the panel init sequence come from Waveshare's BSP and examples, xiaozhi-esp32, and Meta's [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) (Apache-2.0). Voice, link and mood come from [DualEye PC Monitor](https://github.com/EttoreCaputo/esp32s3-dualeye-pcmonitor); the host program is [Companion Hub](https://github.com/EttoreCaputo/companion-hub).
