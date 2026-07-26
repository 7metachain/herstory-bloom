# HerStory Bloom

> 借花传意，让关系自然生长。

![HerStory Bloom poster](docs/herstory-bloom-poster.png)

HerStory Bloom is a voice-enabled smart flowerpot demo built on the Tuya T5AI board. It gives a flower a voice, listens to a user's daily mood, creates a flower diary card, reads local environmental sensors, and can trigger an Insta360 camera to capture a real photo for the diary.

## Features

- Voice conversation and Chinese TTS responses
- “记录日志” mode for creating a flower diary card
- Pixel-garden landscape UI with dismissible cards
- AHT20 temperature/humidity and BMP280 pressure readings
- Automatic watering logic and the voice command “出水”
- Camera trigger through `GET /snap`, with photo status and fallback text card

## Repository layout

This repository contains only the application demo. It is compiled as an application inside the TuyaOpen SDK:

```text
TuyaOpen/
└── apps/tuya.ai/herstory-bloom/
```

## Build from a fresh checkout

```bash
git clone https://github.com/tuya/TuyaOpen.git
cd TuyaOpen
git clone https://github.com/7metachain/herstory-bloom.git apps/tuya.ai/herstory-bloom
. ./export.sh
cd apps/tuya.ai/herstory-bloom
tos.py build
```

The project targets the `T5AI` platform and `TUYA_T5AI_BOARD` with the 3.5-inch LCD configuration.

## Flash and monitor

Close any serial monitor before flashing:

```bash
tos.py flash
tos.py monitor -p /dev/cu.usbmodem5AAE1668883 -b 460800
```

The exact serial device may differ by operating system. The second USB CDC port is normally the application log port.

## Hardware

The AHT20 + BMP280 module uses the red P11 header:

| Sensor | T5 P11 |
| --- | --- |
| VCC/VIN | 3V3 |
| GND | GND |
| SCL | P00 / GPIO0 / I2C1 SCL |
| SDA | P01 / GPIO1 / I2C1 SDA |

The pump control signal is `P07` (P11 pin 7). The pump must use its own suitable power supply and an external transistor/MOSFET or relay driver.

## Diary and camera flow

Say “记录日志”, then speak the diary text. The board generates the AI reply and requests a photo from the configured `/snap` endpoint. If the photo cannot be received or decoded, the original text diary card is still shown.

See [README_CN.md](README_CN.md) for the detailed Chinese wiring and sensor notes.
