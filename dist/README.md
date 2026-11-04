# Miyoo Mini / Onion OS packages

## Folder drop-in (current)

**Folder:** [`green_2026-11-04-03-11/`](./green_2026-11-04-03-11/)  
**Tar:** [`green_2026-11-04-03-11.tar`](./green_2026-11-04-03-11.tar) (~3.4 MB)

Built from master `8dd728c` (per-document zoom persistence, finer pan/zoom; auto-zoom removed).

Install:
1. `App/Green/` → `/mnt/SDCARD/App/Green/`
2. `bin/green/` → `/mnt/SDCARD/.tmp_update/bin/green/`
3. PDFs in `/mnt/SDCARD/Media/PDF/`

`bin/green/lib/*.so.0` are real ELF files (not symlinks) for FAT.

## Notes

- SDL 1.2 ARM EABI5; pangocairo linked from mini sysroot; runtime libs under `bin/green/lib/`.
- Rebuild: `packaging/miyoo/build-arm.sh`
