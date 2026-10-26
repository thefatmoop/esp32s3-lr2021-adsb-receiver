# Flashing and first setup

## Ready-to-flash release

Download and extract `esp32s3-lr2021-adsb-receiver-v1.0.1-firmware.zip` from
[Releases](https://github.com/thefatmoop/esp32s3-lr2021-adsb-receiver/releases). Verify its SHA-256
checksum against `SHA256SUMS.txt` if desired. Use the ESP32-S3 W12 with 16 MB
flash; the image is not for ESP32-P4, ESP32-C5, or SX1262 boards.

Install Python and Espressif's flashing utility:

```sh
python -m pip install esptool
```

Connect the W12 using a USB data cable. Replace `PORT` with the serial port
shown by your OS, such as `COM7` on Windows or `/dev/ttyACM0` on Linux.

### First installation

This replaces firmware and clears existing saved settings, including Wi-Fi
credentials and GNSS position. Preserve any prior firmware/settings you need
before doing it.

```sh
python -m esptool --chip esp32s3 --port PORT erase-flash
python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash 0x0 esp32s3-lr2021-adsb-receiver-v1.0.1-factory.bin
```

If automatic bootloader entry fails, hold BOOT, tap RESET, release BOOT, and
retry with the port that appears. Reset afterward if the board remains in the
bootloader. Close serial monitors before flashing.

### Update an existing W12 ADS-B installation

For the unchanged partition layout, writing only the application at `0x10000`
preserves NVS settings. Do not erase flash or write the factory image when
you intend to retain them.

```sh
python -m esptool --chip esp32s3 --port PORT --baud 460800 write-flash 0x10000 esp32s3-lr2021-adsb-receiver-v1.0.1-app.bin
```

## Wi-Fi provisioning

1. Join **W12-ADSB-SETUP** with password **12341090**.
2. Open **http://192.168.4.1/setup** manually; a captive-portal popup is not
   required. Enter your own 2.4 GHz Wi-Fi name and password.
3. The firmware tests the new connection before committing it. If it fails,
   the previous saved network remains available for rollback.
4. Rejoin your normal network and open the IP shown on the OLED.

If a saved network is unavailable, the receiver retries and then offers setup.
Without updated configuration, setup expires after ten minutes and the device
reboots to retry the saved network. This handles a router that starts more
slowly than the receiver after a power outage. Merely viewing the setup page
does not extend the timer; submitting a configuration does.

An internet connection is not needed on the local Wi-Fi network. Choose
**Offline · USA** for bundled mapping. Online street tiles and opt-in model/photo
lookups need internet. The first boot has no receiver position; without GNSS,
pan/zoom or use Fit for aircraft and leave receiver distances unavailable.

## Reception and diagnostics

The release defaults to the tested **modified LF path**, gain 13, boost 7,
threshold 40, bandwidth index 0, hardware Manchester, interrupt wake, and
correction Observe mode. Confirm antenna and filter work before tuning.
Do not repeatedly retune based only on hits/sec: nearby aircraft traffic changes.

Receiver settings → Receiver diagnostics shows CRC-valid clean messages,
one-bit recovered messages and their share of accepted packets, FIFO status,
and the decoder self-test. Recovery percentage is
`corrected / (clean + corrected)` since boot. Observe counts eligible repairs
without forwarding them; Apply enables accepted repaired messages.

For TCP use the receiver IP and configured port. Beast defaults to **30005**;
AVR is also available. Keep both the web UI and TCP port on a trusted LAN.
