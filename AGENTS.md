# Saturnis Agent Instructions

- Always run the full build and test loop after changes:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- Preserve determinism:
  - no wall-clock timing in emulation logic
  - no random behavior unless using a fixed seed
  - traces must be stable between identical runs
- Prefer correctness over feature breadth.
- If behavior is unknown or incomplete, add a `TODO` and a focused test when possible.
- Do **not** introduce external downloads or runtime network dependencies.
- Do not include BIOS/ROM assets; only load user-provided files.
