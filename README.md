# BMPCC FPVLink v1.0.0

BMPCC FPVLink links a Blackmagic Pocket Cinema Camera to an FPV drone control system using an ESP32-C3 SuperMini. It provides long-range REC/STOP control from a Betaflight RC channel and sends camera status plus active-media remaining record time to DJI OSD Custom Messages.

## Hardware status

**Hardware proven:** Blackmagic Pocket Cinema Camera 4K.

Pocket Cinema Camera 6K-family models use the Blackmagic camera-control protocol and are intended compatibility targets, but they are **not yet hardware verified by this project**.

## Proven features

- Blackmagic Bluetooth connection, pairing and automatic reconnect
- TX16S / Betaflight MSP REC and STOP control
- Configurable RC channel, threshold and switch direction
- DJI OSD `REC` / `STBY` status
- DJI OSD active-media remaining record time
- Correct Slot 1, Slot 2 and USB/Slot 3 active-media selection on BMPCC 4K
- Live remaining-time updates while recording
- Multiple media installed and active-slot switching
- `MEDIA --` when no media is active
- Temporary setup Wi-Fi with automatic shutdown

## Hardware profile

ESP32-C3 SuperMini:

- FC TX -> GPIO6 (ESP RX)
- FC RX -> GPIO7 (ESP TX)
- GND -> GND
- Betaflight MSP: 115200 baud

The default REC/STOP channel is CH11 and can be changed in the web configurator. GPIO6/GPIO7 are fixed for this hardware profile.

## Setup Wi-Fi

After flashing, join `BMPCC-FPVLink-XXXX` with password `fpvcinecam32`, then open `192.168.4.1`. If no device joins within 90 seconds after boot, setup Wi-Fi turns off automatically. It returns on the next reboot.

## Version history

`v1.0.0` is the polished BMPCC-specific release derived from the hardware-proven FPVCineCam32 `v0.10.10` baseline. The proven BLE, MSP, REC/STOP, OSD and 9:2/10:1 media decoding paths are intentionally unchanged. Development-only packet capture and web diagnostics were removed for the product release.

## Project relationship

BMPCC FPVLink is the stable Blackmagic-specific project. Broader multi-camera development continues separately in FPVCineCam32 / CineCamLink32.

## Disclaimer

Independent open-source project. Not affiliated with, endorsed by, or sponsored by Blackmagic Design, DJI, Betaflight, or their respective owners.
