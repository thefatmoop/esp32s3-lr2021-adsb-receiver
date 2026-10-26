# License, source and asset credits

Thank you to the authors and contributors of the projects below. Adapted
radio code, configuration references, development dependencies and design
inspiration are identified separately; this project is not an official
release of any of them.

- **W12 ADS-B firmware:** GPL-3.0-or-later; full license in `LICENSE`.
- **LR2021 radio foundation:** adapted from
  [SAMS0N1TE/lr2021-sdr](https://github.com/SAMS0N1TE/lr2021-sdr), pinned source
  revision `929d97a1b14b9563f418d6f4fd84a787a56e45aa`. The required interface
  header is retained in `vendor/lr2021-sdr/include/`, with its GPL license.
  The W12 HAL and ADS-B receiver are adapted/extended code, not an unmodified
  upstream SDR application.
- **LakeShark:** [SAMS0N1TE/LakeShark](https://github.com/SAMS0N1TE/LakeShark),
  GPL-3.0. The receive-only OOK/Mode S path in `main/radio.c` is adapted from
  `components/lakeshark/board/ls_lora_lr20xx.c`. Its receiver configuration,
  FIFO handling and gain/threshold controls provided the starting point.
- **ADSBee:** [PantsForBirds/adsbee](https://github.com/PantsForBirds/adsbee),
  GPL-3.0, reference revision `2046855bd0565e124530f4dbadf354b32f30db59`.
  The ADSBee 1421 LR2021 implementation informed the hardware Manchester
  mode (2 Mchips/s, inverted encoding, 14 decoded bytes), preamble detection,
  fixed-gain saturation-blanking workaround at `0xF30150`, and clearing the
  CRC-append bit at `0xF30844`. These settings/workarounds are adapted into
  `main/radio.c`; the ADSBee application and hardware design are not bundled.
  Its streaming receiver was also a reference for the TCP-output feature.
- **TheClams LR2021 projects:** [lr2021](https://github.com/TheClams/lr2021)
  (driver reference version 0.14.0) and
  [lr2021-apps](https://github.com/TheClams/lr2021-apps), MIT.
  Used for register/command checks, ADS-B detector configuration and the
  CCA-based automatic threshold approach adapted in `main/radio.c`.
  The MIT copyright and permission notice is retained below.
- **Mode S decoding references:** LakeShark's decoder references
  [watson/libmodes](https://github.com/watson/libmodes), derived from
  [antirez/dump1090](https://github.com/antirez/dump1090). These were consulted
  for Mode S/ADS-B decoding and error-recovery behavior. This project uses its
  compact ESP32-S3 decoder in `main/adsb.c`; the original libmodes source files
  are not bundled. Their BSD 2-Clause notice is retained below as attribution.
- **Semtech driver reference:** [Lora-net/usp](https://github.com/Lora-net/usp),
  particularly `smtc_rac_lib/radio_drivers/lr20xx_driver`, for DIO, RF switch,
  reset and command-format checks against the LR2021 datasheet.
- **Board pin reference:** [meshtastic/firmware](https://github.com/meshtastic/firmware),
  `variants/esp32s3/meshnology-w12`, cross-checked with the W12 schematic.
  Meshtastic firmware is not included or required.
- **Build/runtime dependencies:** [espressif/esp-idf](https://github.com/espressif/esp-idf),
  [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32)
  and [platformio/platformio-core](https://github.com/platformio/platformio-core).
  Documentation screenshot tooling uses [microsoft/playwright](https://github.com/microsoft/playwright).
  Dependency sources and their licenses remain available from those projects;
  versions are pinned in the build configuration where applicable.
- **Map-interface inspiration:** [adsb.lol](https://adsb.lol/) was a visual and
  interaction reference requested during development. This UI is a local
  Canvas implementation; no adsb.lol aircraft feed, branding or frontend
  source is bundled.
- **Offline maps:** [Natural Earth](https://www.naturalearthdata.com/), public
  domain under its [terms](https://www.naturalearthdata.com/about/terms-of-use/).
  Vector sources: [nvkelso/natural-earth-vector](https://github.com/nvkelso/natural-earth-vector).
  Sources are pinned in `map-sources/sources.json`. Ordinary city names and
  geographic coordinates in the nationwide map are public cartographic data,
  not receiver positions or personal tracking records.
- **Optional online map:** © OpenStreetMap contributors. Street tiles are
  fetched only when that map layer is selected. See
  [OpenStreetMap copyright](https://www.openstreetmap.org/copyright).
- **Optional model/photo information:** adsbdb and Airport-Data.com. No aircraft
  photos from these services are bundled in this release. Provider attribution
  remains in the UI when the user opts in.
- **Hardware documentation:** Meshnology's W12 documentation, schematic and
  vendor datasheets are linked from the hardware guide. No manufacturer
  endorsement is implied.
- **Project photographs and enclosure meshes:** supplied by the project
  maintainer for this release. Photos are rotated without altering device
  details. The SAW-filter location and bridge pictures are annotated assembly
  illustrations. Project-supplied assets accompany this GPL-licensed project;
  third-party source materials retain their respective terms.

All web UI screenshots are labeled demonstrations with synthetic aircraft
and a fictional receiver position. No live receiver dump or private history
is part of this repository or its firmware release.

## TheClams MIT notice

```text
MIT License

Copyright (c) 2025 TheClams

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## libmodes / dump1090 BSD notice

```text
BSD 2-Clause License

Copyright (c) 2012, Salvatore Sanfilippo <antirez@gmail.com>
Copyright (c) 2017, Thomas Watson
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
