# Yoru

![Target](https://img.shields.io/badge/target-ESP32--S3-brightgreen)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.1-blue)
![LVGL](https://img.shields.io/badge/LVGL-9.3.0-orange)
![License](https://img.shields.io/badge/license-MIT-green)

A desk companion that lives in the black of a round AMOLED: a pair of glowing eyes with a personality. It is a pet that reacts to you and to your computer, and an AI assistant you can talk to, running on your own computer's models or an API of your choice.

Status: early development (firmware 0.1.0, milestone M1).

- [The character](docs/CONCEPT.md)
- [Roadmap and hardware](docs/ROADMAP.md)

## Hardware

[Waveshare ESP32-S3-Touch-AMOLED-1.75C](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C): ESP32-S3R8, 1.75" 466 × 466 AMOLED with touch, ES8311 + ES7210 audio with two mics, AXP2101 battery management, QMI8658 IMU.

## Build

ESP-IDF 6.1:

```bash
source ~/.espressif/tools/activate_idf_v6.1.sh
idf.py build merge-bin
idf.py -p /dev/cu.usbmodem* flash monitor
```

If the board doesn't show up as a serial port, hold BOOT while plugging it in.

## Credits

Pins and the panel init sequence come from Waveshare's BSP and examples, xiaozhi-esp32, and Meta's [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) (Apache-2.0). Voice, link and mood come from [DualEye PC Monitor](https://github.com/EttoreCaputo/esp32s3-dualeye-pcmonitor).
