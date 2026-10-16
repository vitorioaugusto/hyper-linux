# Contributing

This project was vibe-coded with AI assistance. Contributions still need clear
reasoning, reviewable changes and validation.

Before opening a pull request:

```bash
python3 -m unittest -v test_backend
bash build.sh
QT_QPA_PLATFORM=offscreen ./hyper-linux --preview --smoke-test
```

Describe the concrete problem, the resulting behavior and what was tested.
For hardware changes, include the headset model, USB ID and whether the test
was performed on an actual device. Do not equate packet implementation with
confirmed hardware support. Keep firmware writes outside the current scope.

Do not commit vendor installers, DLLs, firmware, fonts, logos, extracted images
or personal diagnostic logs. Use original or appropriately licensed assets.
Sanitize captures to the minimum reports needed for a test.

By contributing, you agree to license your contribution under this project's
MIT License.

Keep user-facing strings and repository screenshots in English. Regenerate screenshots
with `QT_QPA_PLATFORM=offscreen ./hyper-linux --screenshot docs/screenshot.png`
and `--page 1` / `--page 2` for Audio / Equalizer. The screenshots use demo data.

For packaging changes, run `bash packaging/build-in-container.sh`; see
[packaging notes](docs/packaging.md). Do not commit `build/`, `dist/` or downloaded tools.
