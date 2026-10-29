# Yoru

![Target](https://img.shields.io/badge/target-ESP32--S3-brightgreen)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.1-blue)
![LVGL](https://img.shields.io/badge/LVGL-9.3.0-orange)
![License](https://img.shields.io/badge/license-MIT-green)

A desk companion that lives in the black of a round AMOLED: a small matte sphere with two tiny eyes and two floating hands, in the dark, that you can make your own. It lives a life of its own on the desk, and it is the body of a personal assistant on your computer: it tells you what is worth knowing and answers when you talk to it.

Status: early development, firmware 0.11.9. What is in and what is left: [the roadmap](ROADMAP.md).

- [The character](CONCEPT.md)
- [Roadmap and hardware](ROADMAP.md)
- [Protocol](protocol.md): what differs from DualEye's
- [Firmware changelog](FIRMWARE_CHANGELOG.md)
- [Mascot states](mascot/STATES.md): the Rive file's inputs

## What is on the board

- **Yoru itself:** the sphere with its eyes and hands, moving through a space in perspective. It floats near you, gets sleepy and naps when nobody is around, and comes to the glass when something urgent waits for you. Its needs and the life they drive (fireflies, play, exploring) come back later.
- **Senses:** touch (pokes, strokes, flicks), motion (lifted, shaken, knocked, rocked, face down, dropped, spun, tilted) and where a sudden sound comes from.
- **Voice:** a wake word (Alexa, Hi ESP, Jarvis or Computer, until "Yoru" exists), push-to-talk on PWR, listening on after an answer, and the host's voice on the speaker.
- **The menu,** a watch's honeycomb of round apps around Yoru (swipe down from the top): Notifications, Timers, Habits, Games, Do not disturb and Settings.
- **The host's side:** the inbox of notifications, timers and reminders, your daily habits, the Allow and Deny card before Yoru acts for you, and Yoru's errands while the assistant calls one of your services.
- **Games:** Tilt ball, Slingshot and Constellations.

## Host program

On the computer, Yoru is driven by Yoru Hub (`../yoru-hub`), its desktop app and CLI: a personal assistant that reaches your services through MCP connectors, tells you what is worth knowing, answers by voice, flashes and updates the firmware, pairs Yoru over WiFi and lets other MCP clients use it.

The app carries this firmware: after a change, build it here and run `tools/fetch_firmware.sh` in Yoru Hub.

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

Once it boots, the board talks protocol v2 on the USB port, so `idf.py monitor` only shows the boot messages and then frames. To read its logs and call its tools without Yoru Hub, use the test scripts (pyserial, in the ESP-IDF Python env):

```bash
python3 tools/audio_selftest.py tone
```

`tools/yoru_link.py` is the small library they share: `Link().hello()`, `call("tools/call", {"name": "get_state", "arguments": {}})`, and `frames()` for the log lines.

Say "Alexa" (or another wake word, with `set_wake_word`) and Yoru listens: its eyes open wide and look at you. Without a host nobody answers: if you said something it thinks for up to 30 s, then goes back to its own life.

## Credits

Pins and the panel init sequence come from Waveshare's BSP and examples, xiaozhi-esp32, and Meta's [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) (Apache-2.0). Voice, link and mood come from [DualEye PC Monitor](https://github.com/EttoreCaputo/esp32s3-dualeye-pcmonitor); the host program is Yoru Hub (`../yoru-hub`).

## License

MIT, see [LICENSE](../LICENSE).
