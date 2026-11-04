# Miyoo Mini / Onion OS packages

## Folder drop-in (current)

**Folder:** [`green_2026-11-04-02:57/`](./green_2026-11-04-02:57/)  
**Tar (easy download):** [`green_2026-11-04-02:57.tar`](./green_2026-11-04-02:57.tar) (~3.4 MB)

Built from master `33b450a` (auto-zoom, wider bookmarks, scoped launch libs, Pango Arabic TOC).

Install:
1. `App/Green/` → `/mnt/SDCARD/App/Green/`
2. `bin/green/` → `/mnt/SDCARD/.tmp_update/bin/green/`
3. PDFs in `/mnt/SDCARD/Media/PDF/`

`bin/green/lib/*.so.0` are **real ELF files** (not symlinks) for FAT SD cards.

## Older zip drop-in

[`green-onion-dropin.zip`](./green-onion-dropin.zip) — superseded by the folder/tar above.

## Notes

- SDL 1.2 ARM EABI5; linked to pangocairo/pango/harfbuzz/fribidi from mini sysroot; runtime libs bundled under `bin/green/lib/`.
- Rebuild: `packaging/miyoo/build-arm.sh`
