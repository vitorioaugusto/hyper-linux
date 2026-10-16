# Hyper Linux

**English** | [Português (Brasil)](README.pt-BR.md)

> [!IMPORTANT]
> **This entire project was vibe-coded with AI assistance.** The code was built iteratively through prompting, testing, debugging and refinement rather than through a traditional from-scratch coding workflow.

A native Linux desktop app for the **HyperX Cloud III Wireless**, built with Qt 6, Python and PipeWire. No Wine, NGENUITY installation or HyperHeadset dependency is required.

**Status:** experimental, tested with a `03f0:05b7` dongle on CachyOS / KDE Plasma. Battery, mute and microphone monitoring were confirmed during testing. The `03f0:0c9d` variant is recognized but has not been tested on hardware here. EQ audio output still needs broader testing. The application interface and repository screenshots are in English.

Hyper Linux is an independent community project, not affiliated with HyperX or HP. It uses original illustrations and system fonts; no vendor logos, product photos, fonts, Windows installers, DLLs or firmware are distributed.

![Hyper Linux interface](docs/screenshot.png)

## Features

- Battery percentage and charging status, read directly from the dongle.
- One microphone mute control, following the Linux audio server and headset button events.
- Hardware sidetone and a separate PipeWire microphone monitor with adjustable volume.
- Automatic shutdown timer.
- Output volume and microphone gain for the headset's audio devices.
- 10-band software EQ with adjustable frequency and Q, presets and JSON profile import/export.
- Local diagnostics and USB traces, without recording microphone audio.

## Download and install

Download the **AppImage** or **.deb** from [GitHub Releases](https://github.com/vitorioaugusto/hyper-linux/releases/latest). Packages currently target **x86_64 / amd64**, built on Ubuntu 22.04 (glibc 2.35). Other architectures are not packaged yet.

### AppImage

Qt, Python and libusb are included. Your desktop still needs a running **PipeWire session with PulseAudio compatibility**, plus the host `pipewire` and `pactl` commands. On Arch / CachyOS, those are provided by `pipewire`, `pipewire-pulse` and `libpulse`.

```bash
chmod +x Hyper_Linux-0.2.1-x86_64.AppImage
./Hyper_Linux-0.2.1-x86_64.AppImage
```

If your distribution cannot mount AppImages through FUSE, use:

```bash
./Hyper_Linux-0.2.1-x86_64.AppImage --appimage-extract-and-run
```

The AppImage does not install USB permission rules or modify system audio services. See **USB permissions** below if needed.

### Debian / Ubuntu

```bash
sudo apt install ./hyper-linux_0.2.1_amd64.deb
hyper-linux
```

The package installs an application menu entry, runtime dependencies and device-specific udev rules. Reconnect the dongle after installation. An active PipeWire desktop session with PulseAudio compatibility is required; the package does not replace your running audio server.

There is currently no RPM or Flathub publication. See [packaging notes](docs/packaging.md) for build recipes, dependency licenses and validation.

## Source dependencies

For an Arch source build:

```bash
sudo pacman -S --needed gcc pkgconf qt6-base python pipewire pipewire-audio pipewire-pulse libpulse libusb
```

For Ubuntu / Debian:

```bash
sudo apt install g++ pkg-config qt6-base-dev python3 pipewire pipewire-bin pipewire-pulse pulseaudio-utils libusb-1.0-0
```

A powered-on Cloud III Wireless and its USB dongle are required for hardware controls. Run the app as your normal desktop user.

## Build and run

```bash
git clone https://github.com/vitorioaugusto/hyper-linux.git
cd hyper-linux
bash build.sh
bash run.sh
```

Run the app as your normal desktop user, without `sudo`. Close other headset control apps while testing.

### Install for your user

```bash
bash install.sh
```

This installs the app under `~/.local/share/hyper-linux`, adds a launcher under `~/.local/bin`, and creates a **Hyper Linux** application menu entry. `XDG_DATA_HOME` is respected. It does not install system dependencies or modify audio service configuration.

### USB permissions, if needed

If diagnostics report permission denied for `/dev/hidraw*`, install the supplied device-specific rules:

```bash
sudo install -m644 udev/70-hyper-linux.rules /etc/udev/rules.d/70-hyper-linux.rules
sudo udevadm control --reload-rules
```

Unplug and reconnect the dongle. These rules grant access to the active local user; the app itself should still run without `sudo`.

## Microphone monitoring

On **Device**, enable **Hear my voice in the headset** to use hardware sidetone. If it is not audible, disable it and try **Audio → Enable software monitoring**. Start with the default 25% monitor volume and use only one monitoring mode at a time to avoid echo.

The software monitor requests a 10 ms PipeWire latency; actual end-to-end latency depends on the audio graph and headset and has not been measured. Closing the app stops its software monitoring and EQ processes.

## Equalizer

Select the physical headset output on **Audio**, then enable the EQ on **Equalizer**. In your desktop sound settings, select **Hyper Linux · Equalizer** as the output. The app does not change your default audio device automatically. Positive boosts apply conservative headroom to reduce clipping, which may lower the overall volume.

Profiles use the `hyper-linux-eq-v1` JSON format. The EQ is processed by PipeWire and is not saved into the headset's onboard memory.

## Limitations

- No DTS/SONAMI spatial audio or proprietary AI voice processing.
- No firmware updater.
- No assumption of compatibility with the Cloud III S or wired Cloud III.
- No hardware sidetone-level or onboard EQ control for the original Cloud III Wireless.
- Graphical EQ edits recreate the filter node when applied; you may need to select the EQ output again.

## Diagnostics and tests

```bash
./hyper-linux --diagnose
python3 -m unittest -v test_backend
QT_QPA_PLATFORM=offscreen ./hyper-linux --preview --smoke-test
```

The diagnostic command performs read-only queries. Logs are stored in `~/.local/state/hyper-linux`, respecting `XDG_STATE_HOME`. Review logs before attaching them to issues; they can contain local device identifiers and filesystem paths. Include your distribution, desktop, dongle USB ID and the observed behavior when reporting an issue.

Additional screenshots: [Audio](docs/audio.png) · [Equalizer](docs/equalizer.png).

See [the protocol notes](docs/protocol.md) for packet structure and confirmation behavior.

## License

The project code and original artwork are licensed under the [MIT License](LICENSE). Qt and other dependencies retain their own licenses. The AppImage includes third-party notices under `usr/share/licenses/hyper-linux` in its extracted contents. Product names identify compatible hardware.
