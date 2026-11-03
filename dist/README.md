# Miyoo Mini / Onion OS packages

## Small drop-in (recommended)

**Artifact:** [`green-onion-dropin.zip`](./green-onion-dropin.zip) (~1.2 MB)

Onion App launcher + ARM `green` (Pango Arabic TOC) + DejaVuSans + optional pango/harfbuzz libs.

1. Copy `1_RESTORE_App_Green/*` → `/mnt/SDCARD/App/Green/`
2. Copy `2_REPLACE_tmp_update_bin_green/*` → `/mnt/SDCARD/.tmp_update/bin/green/`
3. If TOC fails on missing pango libs, copy `3_OPTIONAL_parasyte_libs/*` → `/mnt/SDCARD/.tmp_update/lib/parasyte/`
4. PDFs in `/mnt/SDCARD/Media/PDF/`

See `INSTALL.txt` inside the zip.

## Full standalone package

**Artifact:** [`green-onion-miyoo.zip`](./green-onion-miyoo.zip) (~75 MB)

Older Steward-style full app tree (may lag the drop-in’s Pango TOC binary). Prefer the drop-in above.

## Notes

- Binary is **SDL 1.2**, ARM EABI5 hard-float, linked to `libpangocairo` / `libpango` / `libharfbuzz`.
- `launch.sh` sets `FONTCONFIG_FILE` to the green fonts.conf (DejaVuSans for Arabic TOC).
- Rebuild: `packaging/miyoo/build-arm.sh`
