# Miyoo Mini / Onion OS packages

## Small drop-in (recommended)

**Artifact:** [`green-onion-dropin.zip`](./green-onion-dropin.zip) (~1.5 MB)

Onion App launcher + ARM `green` (Pango Arabic TOC) + DejaVuSans + bundled pango/harfbuzz under `lib/`.

1. Copy `1_RESTORE_App_Green/*` → `/mnt/SDCARD/App/Green/`
2. Copy `2_REPLACE_tmp_update_bin_green/*` → `/mnt/SDCARD/.tmp_update/bin/green/`
   (includes `green`, `fonts.conf`, `fonts/`, `lib/*.so.0` as real ELF files — not symlinks — for FAT)
3. PDFs in `/mnt/SDCARD/Media/PDF/`

`launch.sh` prepends `$greendir/lib` then Parasyte on `LD_LIBRARY_PATH`.

See `INSTALL.txt` inside the zip.

## Full standalone package

**Artifact:** [`green-onion-miyoo.zip`](./green-onion-miyoo.zip) (~75 MB)

Older Steward-style full app tree. Prefer the drop-in above.

## Notes

- Binary is **SDL 1.2**, ARM EABI5 hard-float, linked to `libpangocairo` / `libpango` / `libharfbuzz` / `libfribidi`.
- Cross-build uses mini sysroot pango headers/libs; runtime pango stack is bundled in the drop-in `lib/` (stock Onion Parasyte often lacks them).
- Rebuild: `packaging/miyoo/build-arm.sh`
