# Miyoo Mini / Onion OS packages

## Folder drop-in (current)

**Folder:** [`green_2026-11-04-03-15/`](./green_2026-11-04-03-15/)  
**Tar:** [`green_2026-11-04-03-15.tar`](./green_2026-11-04-03-15.tar) (~3.4 MB)

Built from master `5f9ba99` (blank-screen fix after in-app file picker close).

Install:
1. `App/Green/` → `/mnt/SDCARD/App/Green/`
2. `bin/green/` → `/mnt/SDCARD/.tmp_update/bin/green/`
3. PDFs in `/mnt/SDCARD/Media/PDF/`

`bin/green/lib/*.so.0` are real ELF files (not symlinks) for FAT.
