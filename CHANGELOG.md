# Changelog

## v1.0.0 — 2026-10-25

Initial public release of the standalone W12 ADS-B receiver:

- LF 1090 MHz reception with LR2021 hardware Manchester decoding.
- ESP32-S3 CRC validation, conservative one-bit recovery and aircraft decoding.
- Local web UI, offline USA map, smooth buffered motion, bounded prediction,
  aligned trails and GNSS follow mode.
- OLED reception/Wi-Fi/GNSS pages, Wi-Fi provisioning and reconnect behavior.
- Beast/AVR TCP output and receiver-side trail cache.
- Hardware/filter instructions, printable enclosure meshes and demonstration
  screenshots. No Wi-Fi credentials, installation location or device NVS data.
