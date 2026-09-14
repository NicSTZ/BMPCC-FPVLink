# BMPCC-FPVLink v1.0.0

Long-range FPV camera control and DJI OSD telemetry for Blackmagic Pocket Cinema Cameras.

BMPCC-FPVLink connects an ESP32-C3 to a Blackmagic Pocket Cinema Camera over Bluetooth and interfaces with a Betaflight flight controller over MSP.

## Hardware-proven

Tested on:

- Blackmagic Pocket Cinema Camera 4K
- ESP32-C3 SuperMini
- Betaflight / MSP
- DJI digital FPV OSD
- RadioMaster TX16S

## Features

- Blackmagic Bluetooth connection and control
- RC switch REC / STOP control through Betaflight MSP
- DJI OSD REC / STBY messages
- Active media detection
- Slot 1, Slot 2 and USB media support
- Remaining recording time for the active media
- Live remaining-time updates while recording
- Active-media switching with multiple media inserted
- No-media detection
- Configurable RC channel
- Temporary Wi-Fi setup interface
- Automatic Wi-Fi shutdown after setup
- Stable Wi-Fi / Bluetooth coexistence

## Wiring

ESP32-C3 SuperMini:

- GPIO6 = RX
- GPIO7 = TX
- GND = GND

Flight controller TX connects to ESP32 GPIO6.

Flight controller RX connects to ESP32 GPIO7.

MSP baud rate: **115200**

## Status

v1.0.0 is the first hardware-proven BMPCC-FPVLink release.

The Blackmagic Pocket Cinema Camera 4K implementation is considered the stable baseline for future development.

## Project

BMPCC-FPVLink is the Blackmagic-specific implementation developed from the broader FPVCineCam32 / CineCamLink32 project.

Future camera implementations can be developed independently without modifying this proven Blackmagic baseline.

## License

MIT License
