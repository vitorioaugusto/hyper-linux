> **This entire project was vibe-coded with AI assistance.** Experimental community software, released under the MIT License.

- Fix AppImages running without a visible window on KDE/Wayland, including when launched through Gear Lever.
- Bundle Qt Wayland shell, graphics and decoration integrations and their dependencies.
- Verify window exposure and the Wayland compositor configure handshake during startup tests.
- Add a headless Weston test and a regression check that deliberately removes shell integrations.
- Keep the English interface, MIT license and original artwork.

**AppImage:** download, run `chmod +x Hyper_Linux-0.2.2-x86_64.AppImage`, then open it. If FUSE is unavailable, pass `--appimage-extract-and-run`.


A running PipeWire desktop session with PulseAudio compatibility and the host `pipewire` / `pactl` tools is required. Built on Ubuntu 22.04 (glibc 2.35). Packages are x86_64 only. AppImage users may need to install the supplied udev rule; see the README.

Hardware controls were tested with a Cloud III Wireless `03f0:05b7` dongle on CachyOS. The `03f0:0c9d` variant and EQ audio output need further hardware validation. No firmware updater, proprietary spatial audio or Flathub publication is included.

[Installation and source](https://github.com/vitorioaugusto/hyper-linux#download-and-install) · [Português](https://github.com/vitorioaugusto/hyper-linux/blob/main/README.pt-BR.md)
