# BMPCC FPVLink v1.0.0 — release regression test

This is a focused release check. The camera-control/media implementation is inherited unchanged from the hardware-proven v0.10.10 Blackmagic baseline.

1. Flash the GitHub-built v1.0.0 firmware.
2. Confirm setup AP is named `BMPCC-FPVLink-XXXX` and the configurator shows `BMPCC FPVLink v1.0.0`.
3. Confirm saved BMPCC 4K pairing/reconnect still works.
4. Confirm TX16S switch: STBY -> REC -> STBY.
5. Confirm DJI OSD shows `REC` / `STBY`.
6. Confirm media remaining updates for Slot 1, Slot 2 and USB/Slot 3.
7. With no media active, confirm `MEDIA --`.
8. Confirm MSP stays connected during the test.
9. Confirm setup Wi-Fi still shuts down after 90 seconds idle and returns after reboot.

Release only after this regression passes on hardware.
