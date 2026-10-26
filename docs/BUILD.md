# Build from source

Use Python with PlatformIO Core, or the PlatformIO extension in VS Code:

```sh
python -m pip install platformio
pio run -e w12_stable
pio run -e w12_stable -t upload --upload-port PORT
```

Run commands from the repository root. The first build downloads the pinned
pioarduino ESP32 platform and ESP-IDF dependencies. Subsequent builds can use
the downloaded toolchain. All application assets and the compressed offline map
are embedded.

The tested build pins platform `55.03.312-1`. The 16 MB board uses a 4 MB app
partition at `0x10000`. PlatformIO may report a larger board-level limit;
the partition table is authoritative. Outputs are under `.pio/build/w12_stable/`.

## Native tests

Use a C compiler with C11 support (GCC, Clang, or Zig's `cc` frontend):

```sh
cc -std=c11 -Imain tests/check_adsb.c main/adsb.c -lm -o check_adsb
./check_adsb
cc -std=c11 -Imain tests/test_correction.c main/adsb.c -lm -o test_correction
./test_correction
cc -std=c11 -Imain tests/check_wifi_policy.c -o check_wifi_policy
./check_wifi_policy
cc -std=c11 -Imain -Itests/stubs tests/test_stream.c main/trail_cache.c -lm -o test_stream
./test_stream
```

On Windows, use `.exe` output names and run the corresponding executables.
These tests cover CRC/CPR/fields, all single-bit positions and two-bit rejection
in a reference frame, recovery safeguards, Wi-Fi timeout policy, Beast escaping,
and compatibility with browser-only trails. Boot-time receiver self-tests provide an additional check.

## Documentation screenshots

Screenshots are generated from the real UI with synthetic data, without
connecting to a receiver. To regenerate them:

```sh
npm install
npx playwright install chromium
npm run screenshots
```

## Offline map provenance

The ready-to-build `main/world.json.gz` is checked in. `map-sources/sources.json`
lists pinned Natural Earth inputs. To regenerate the map, download each URL
to `map-sources/<name>.geojson`, then run:

```sh
python scripts/build_offline_map.py
```

The script uses only Python's standard library and updates JSON, gzip and the
map ETag. Natural Earth data is public domain; see [THIRD_PARTY.md](../THIRD_PARTY.md).

HTTP and browser polling regression checks:

```sh
python tests/test_web_serialization.py cc
node tests/test_connection.cjs
```
