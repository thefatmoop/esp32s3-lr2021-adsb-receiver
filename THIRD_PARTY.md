# License, source and asset credits

- **W12 ADS-B firmware:** GPL-3.0-or-later; full license in `LICENSE`.
- **LR2021 radio foundation:** adapted from
  [SAMS0N1TE/lr2021-sdr](https://github.com/SAMS0N1TE/lr2021-sdr), pinned source
  revision `929d97a1b14b9563f418d6f4fd84a787a56e45aa`. The required interface
  header is retained in `vendor/lr2021-sdr/include/`, with its GPL license.
  The W12 HAL and ADS-B receiver are adapted/extended code, not an unmodified
  upstream SDR application.
- **LR2021 ADS-B reference:** [TheClams/lr2021-apps](https://github.com/TheClams/lr2021-apps)
  provided a useful reference for LR2021 ADS-B radio configuration. See its
  original repository for its license and supported hardware.
- **Offline maps:** [Natural Earth](https://www.naturalearthdata.com/), public
  domain under its [terms](https://www.naturalearthdata.com/about/terms-of-use/).
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
