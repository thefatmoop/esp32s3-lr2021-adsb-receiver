# Changelog

## v1.0.1 — 2026-10-26

- Reduce live polling to 2 Hz while retaining smooth browser animation.
- Serialize each browser's receiver requests and tolerate isolated poll failures.
- Stream status aircraft data in bounded chunks to reduce peak heap use.
- Move trails to browsers and reserve the HTTP snapshot buffer; remove the redundant USB status aircraft copy.
- Allow eight HTTP sessions, bound temporary Wi-Fi buffers, and expose disconnect/reset diagnostics.

## v1.0.0 — 2026-10-25

Initial public release of the standalone W12 ADS-B receiver:

- LF 1090 MHz reception with LR2021 hardware Manchester decoding.
- ESP32-S3 CRC validation, conservative one-bit recovery and aircraft decoding.
- Local web UI, offline USA map, smooth buffered motion, bounded prediction,
  aligned trails and GNSS follow mode.
- OLED reception/Wi-Fi/GNSS pages, Wi-Fi provisioning and reconnect behavior.
- Beast/AVR TCP output and receiver-side trail cache.
- Hardware/filter instructions, printable enclosure meshes and demonstration
  screenshots.
