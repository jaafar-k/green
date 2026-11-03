# Miyoo Mini / Onion OS package

**Artifact:** [`green-onion-miyoo.zip`](./green-onion-miyoo.zip) (~75 MB)

Cross-compiled ARM build of this repo’s `green` (bookmarks, last-page restore, preview rendering, Miyoo controls), packaged like Steward Fu’s Miyoo green app.

## Install

1. Extract the zip and copy the `green/` folder to the SD card:
   - `/mnt/SDCARD/App/green`
2. Place PDFs in `/mnt/SDCARD/PDF/` (matches `config.json` `rompath`).
3. In Onion, open **Apps → PDF Reader (Green+)**.

## Parasyte

`libs/` includes poppler plus bundled glib/cairo and related deps so the app can often run **without** Parasyte.

- Try without Parasyte first.
- If launch fails on a missing library, install Steward Fu’s **Parasyte** under `/mnt/SDCARD/App/parasyte/`. `launch.sh` still falls back to that rootfs on `LD_LIBRARY_PATH`.

## Notes

- Binary is **SDL 1.2** (not SDL2), ARM EABI5 hard-float.
- `HOME` is the app directory, so `.green.pages` / `.green.bookmarks` live next to the binary.
- Rebuild details: see `packaging/miyoo/` in this repo.
