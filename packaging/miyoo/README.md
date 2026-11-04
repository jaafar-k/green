# Miyoo packaging

1. Obtain Steward Fu assets (`rel_green_20220214.zip`) and toolchains (`onion_toolchain.tar.gz`, `mini_toolchain-v1.0.tar.gz`) from his miyoo-mini GitHub release.
2. Stage poppler **0.84.0** glib headers (matches bundled `libpoppler.so.94`).
3. Run `./build-arm.sh` to produce `build-miyoo/green` (ARM ELF). Uses `pkg-config pangocairo` from the mini sysroot (`PKG_CONFIG_LIBDIR` must include `usr/share/pkgconfig` for xproto).
4. Assemble `dist/green-onion-dropin.zip` (App restore + `.tmp_update/bin/green` + fonts + optional pango libs from the sysroot).
