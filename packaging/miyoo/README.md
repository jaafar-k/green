# Miyoo packaging

1. Obtain Steward Fu assets (`rel_green_20220214.zip`) and toolchains (`onion_toolchain.tar.gz`, `mini_toolchain-v1.0.tar.gz`) from his miyoo-mini GitHub release.
2. Stage poppler **0.84.0** glib headers (matches bundled `libpoppler.so.94`).
3. Run `./build-arm.sh` to produce `build-miyoo/green` (ARM ELF).
4. Assemble app folder from Steward’s `green/` package, replace binary, update `config.json` label, zip as `dist/green-onion-miyoo.zip`.
