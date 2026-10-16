> **This entire project was vibe-coded with AI assistance.** Experimental community software, released under the MIT License.

- English application interface, diagnostics and repository screenshots.
- x86_64 AppImage with Qt, Python and libusb included.
- amd64 Debian/Ubuntu package with desktop launcher, dependencies and USB permission rules.
- Automated package builds, startup checks and SHA256 checksums.

**AppImage:** download, run `chmod +x Hyper_Linux-0.2.0-x86_64.AppImage`, then open it. If FUSE is unavailable, pass `--appimage-extract-and-run`.

**Debian/Ubuntu:** `sudo apt install ./hyper-linux_0.2.0_amd64.deb`. Reconnect the dongle after installation.

A running PipeWire desktop session with PulseAudio compatibility and the host `pipewire` / `pactl` tools is required. Built on Ubuntu 22.04 (glibc 2.35). Packages are x86_64 only. AppImage users may need to install the supplied udev rule; see the README.

Hardware controls were tested with a Cloud III Wireless `03f0:05b7` dongle on CachyOS. The `03f0:0c9d` variant and EQ audio output need further hardware validation. No firmware updater, proprietary spatial audio or Flathub publication is included.

[Installation and source](https://github.com/vitorioaugusto/hyper-linux#download-and-install) · [Português](https://github.com/vitorioaugusto/hyper-linux/blob/main/README.pt-BR.md)
