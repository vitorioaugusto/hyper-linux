# Linux packages

The application and repository screenshots use English. The Portuguese README is maintained separately. The code and original artwork remain MIT licensed; bundled dependencies keep their own licenses.

## Formats

- **AppImage (x86_64):** Qt 6, Python backend frozen with PyInstaller, libusb and shared runtime libraries. Uses the host's `pactl`, `pipewire` and active desktop audio session. It does not install udev rules or change system services.

The AppImage is built on Ubuntu 22.04 with glibc 2.35. A newer Linux desktop is required; packaging does not guarantee compatibility with every distribution. RPM, ARM and Flatpak/Flathub are not currently provided. Flatpak would need a separate recipe, hardware/audio sandbox testing and a Flathub submission.

## Build

From the repository root, with Podman or Docker installed:

```bash
bash packaging/build-in-container.sh
```

The container image installs the compiler, Qt development packages and PyInstaller. The recipe runs backend tests, builds the Qt application, creates the AppImage, checks its startup and backend runtime, and writes SHA256 checksums. Results are in `dist/`.

Verify the AppImage in a minimal Ubuntu container without Qt/Python, including the Wayland compositor handshake and missing-shell regression:

```bash
bash packaging/test-clean-containers.sh
```

This checks startup and bundled runtime dependencies; it cannot check a physical headset or a desktop audio session.

For an already configured Ubuntu 22.04 builder:

```bash
bash packaging/build-packages.sh
```

The AppImage tools are downloaded from upstream GitHub releases and verified against the SHA256 values in `packaging/tools.json`. Update a URL and its digest together when intentionally upgrading a tool. Build outputs and downloaded tools are ignored by Git.

## Runtime checks

```bash
APPIMAGE_EXTRACT_AND_RUN=1 dist/Hyper_Linux-0.2.2-x86_64.AppImage --version
APPIMAGE_EXTRACT_AND_RUN=1 dist/Hyper_Linux-0.2.2-x86_64.AppImage --backend-info
APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen dist/Hyper_Linux-0.2.2-x86_64.AppImage --preview --smoke-test
```

`--backend-info` reports `frozen: true` for the AppImage and does not access hardware. `--diagnose` performs read-only device and audio queries; run it in your desktop session to check USB access. Preview and offscreen checks do not validate audible monitoring or EQ output.

The AppImage includes the Wayland shell, client graphics and decoration plugin directories in addition to the platform plugin. `packaging/test-wayland.sh` runs the packaged app under headless Weston and checks the compositor configure handshake as well as window exposure. This runs in a clean runtime without system Qt packages, so missing bundled plugins cannot fall back to host Qt. It also removes shell integration plugins from an extracted copy and verifies the shell error and absence of the configure handshake, reproducing the original invisible-window bug. Install `weston` and `ripgrep` to run this check outside the builder.

The launcher clears external Qt plugin overrides. Before starting host audio tools, the backend restores the original library path so bundled Qt/Python libraries do not interfere with system PipeWire tools.

## USB access

Obtain the device-specific rule from this repository (or extract the AppImage and use `squashfs-root/usr/share/hyper-linux/70-hyper-linux.rules`), then follow the README's USB permission instructions. Reconnect the dongle. Run the app without sudo.

## Licenses and library replacement

Extract with `--appimage-extract`. Original source is in this repository. The extracted `usr/share/licenses/hyper-linux/` directory includes full common license texts, distribution copyright notices, exact Ubuntu package versions and notices for the Python freezing tools. Ubuntu source packages are available through Launchpad using the recorded source package and version. Qt is dynamically linked; bundled shared libraries can be inspected or replaced in the extracted directory and the application started through `AppRun`.

## Publishing

`VERSION` is the application and package version. Update the AppStream release metadata and release notes when changing it. GitHub Actions builds packages for pull requests and main pushes. A main-branch commit whose message starts with `release:` publishes `v<VERSION>` only after the build and tests pass, with the AppImage and `SHA256SUMS` attached. The release job alone receives permission to write repository contents. Existing release tags are not overwritten by the workflow.
