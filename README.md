# TiltBack

TiltBack is a Linux tablet orientation clinic. It shows when the display, finger, stylus, and pointer disagree about which way is up, lets you correct each layer independently, and remembers the answer for that chassis.

On Windows those four transforms are usually kept in sync. On Linux they live in separate subsystems (DRM panel-orientation, the compositor output transform, evdev / libinput, and the hardware cursor). Each layer can be correct in isolation and still leave a tablet unusable: the picture is upside down, a tap lands beside the contact, or the pen draws offset.

The name is literal: tilt the stack back to the orientation the hardware was built for.

![TiltBack clinic dashboard](https://i.imgur.com/QPFoIMn.png)

## What it does

One C++ / Qt Quick binary (`tiltback`) provides the dashboard, session apply, persist, and follow.

- **Picture (T)** — rotate the builtin panel: None, Left, Inverted, Right.
- **Finger / Pen (R)** — set a residual on the named digitizers (`R=0 / 1 / 2 / 4 / 8`). Devices are identified by name and USB/I2C VID:PID, never by `eventN`.
- **Keep / Revert** — each apply has an independent 10-second countdown so a bad guess can be undone.
- **Save home** — stores `(T, R_touch, R_pen)` in `~/.config/tiltback/home.json` (Plasma also writes `kcminputrc`).
- **Follow** — a user systemd unit restamps residuals when the compositor changes T. Follow never writes the picture transform.
- **Report** — `--report` prints a four-layer dump for bug reports.

Arrow is diagnose-only in this release. Phosh / phoc is not a backend.

## Supported sessions

The runtime picks a backend from the live session. Do not mix a glibc build with a musl tablet, or the reverse.

| Session | Picture | Residual |
| --- | --- | --- |
| KDE Plasma (KWin Wayland) | KScreen / `kscreen-doctor` | KWin `orientationDBus` |
| GNOME (Mutter Wayland) | Mutter DisplayConfig / `gdctl` | udev `LIBINPUT_CALIBRATION_MATRIX` and a HID rebind |
| X11 (RandR + XInput) | `xrandr --rotate` | XInput CTM, or Wacom Rotation on xf86-input-wacom nodes |

On GNOME, Mutter already composes T onto the touchscreen, so the finger residual is a **constant** leftover. The integrated stylus does not follow T; follow writes a composed `R(T)` and rebinds the HID devices. Packaged installs use the `tiltback-gnome` helper (`tiltback-rebind.path`) so that rebind does not prompt for a password.

## Proven chassis

| Machine | Session | Home that worked |
| --- | --- | --- |
| Samsung Galaxy Book 10.6 (SM-W620) | postmarketOS Plasma 6 Wayland | `T=Rotated90`, `R_touch=8`, `R_pen=8` |
| Samsung Galaxy Book 10.6 (SM-W620) | postmarketOS GNOME / Mutter Wayland | `T=Rotated180`, `R_touch=8`, `R_pen=2` |
| raytrektab RT08WT | postmarketOS Plasma Wayland | `T=Rotated180`, `R_touch=8`, `R_pen=8` |
| Acer Chromebook Tab 10 | Debian 13 XFCE X11 | `T=Normal`, `R_touch=0`, `R_pen=0` |

The Plasma W620 seed is not applied on GNOME. The same DMI can need a different home tuple under a different compositor.

## Build and install

See [docs/build.md](docs/build.md) for host packages, prefixes, follow, and Alpine / postmarketOS packaging.

On the machine that will run TiltBack:

```sh
./scripts/deps.sh
./scripts/build.sh --install
```

That installs to `~/.local` (binary, desktop file, and hicolor icons). Launch it from the desktop application menu.

Cross-building for postmarketOS from a glibc PC:

```sh
./scripts/build-apk.sh
# then on the tablet:
sudo apk add --allow-untrusted ./tiltback-*.apk ./tiltback-gnome-*.apk
```

`tiltback-gnome` is pulled automatically when `gnome-shell` and `systemd` are present. After an apk install, use `/usr/bin/tiltback` (or the GNOME application menu). Close an already-open window and launch again; a leftover `~/.local` desktop file wins over the packaged one.

QML is interpreted (`NO_CACHEGEN`) so an Alpine 3.22 (Qt 6.8) build can load on postmarketOS 26.06 (Qt 6.11).

## Using the clinic

1. Open TiltBack on the graphical session (not a headless SSH without the session bus).
2. Apply **Picture** until the panel is upright. Keep or revert.
3. Apply **Finger** and **Pen** until a tap and a stylus contact land on the pixel they appear to belong to.
4. **Save home**.
5. **Install follow**, then enable the user unit:

```sh
tiltback --install-follow
systemctl --user enable --now tiltback-follow.service
```

`--install-follow` prefers `/usr/bin/tiltback` when that file exists. The GUI process must not also run `--follow`.

Other entry points: `tiltback --report`, `tiltback --save-home`. Never `pkill -f tiltback` (it matches SSH).

## Documentation

| Document | Contents |
| --- | --- |
| [docs/build.md](docs/build.md) | Host packages, install prefixes, follow, Alpine apk, GNOME helper |
| [docs/concept-and-feasibility.md](docs/concept-and-feasibility.md) | Model, four layers, current state, what is not shipped |
| [docs/home-offset-and-follow.md](docs/home-offset-and-follow.md) | Home tuple and why follow exists |
| [docs/case-galaxy-book-w620.md](docs/case-galaxy-book-w620.md) | First clinic case (W620 on Plasma) |
| [docs/w620-runbook.md](docs/w620-runbook.md) | Measured W620 values and how to re-apply |

## License

Copyright 2026 The TiltBack authors.

TiltBack is free software: you can redistribute it and/or modify it under the terms of the [GNU General Public License](LICENSE), version 3 or (at your option) any later version.
