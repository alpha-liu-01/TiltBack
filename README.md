# TiltBack

TiltBack is a Linux tablet orientation clinic. It shows when the display, finger, stylus, and pointer disagree about which way is up, lets you correct each layer independently, and remembers the answer for that chassis.

On Windows those four transforms are usually kept in sync. On Linux they live in separate subsystems (DRM panel-orientation, the compositor output transform, evdev / libinput, and the hardware cursor). Each layer can be correct in isolation and still leave a tablet unusable: the picture is upside down, a tap lands beside the contact, or the pen draws offset.

The name is literal: tilt the stack back to the orientation the hardware was built for.

![TiltBack clinic dashboard](https://i.imgur.com/QPFoIMn.png)

## What it does

One C++ / Qt Quick binary (`tiltback`) provides the dashboard, session apply, persist, and follow.

- **Picture (T)** — rotate the builtin panel: None, Left, Inverted, Right.
- **Finger / Pen (R)** — set a residual on the named digitizers (`R=0 / 1 / 2 / 4 / 8`). Devices are identified by name and USB/I2C VID:PID, never by `eventN`.
- **Tilt** — corrects a wrong accelerometer frame so auto-rotate picks the `T` that matches the hold (classically two landscapes swapped, or two portraits swapped). The card classifies the IMU as no sensor, unreadable, or readable. On a readable IMU, Prev / Next steps ten `ACCEL_MOUNT_MATRIX` values (eight panel-plane / Z-sign plus 180° about X and Y); Identity and Wiki jump to those two; Bottom / Right / Top / Left mean “this panel bezel is down,” and Solve snaps to the nearest of the ten. Apply is a udev rule via `tiltback-accel.path` and an `iio-sensor-proxy` reload, not `LIBINPUT_CALIBRATION_MATRIX` and not `--follow` writing `T`. A dead IIO ring buffer still counts as readable when sysfs poll works. During the Tilt countdown, follow stays running so taps still land (Picture / Finger / Pen still mute follow so those clinic writes are not overwritten).
- **Keep / Revert** — each apply has an independent 10-second countdown so a bad guess can be undone. Tilt’s banner is a sensor reload. Revert restores the matrix that was live when the cycle started.
- **Save home** — stores `(T, R_touch, R_pen)` and `accelMountMatrix` in `~/.config/tiltback/home.json` (Plasma also writes `kcminputrc`). Follow does not apply the mount matrix.
- **Follow** — a user systemd unit restamps residuals when the compositor changes T. Follow never writes the picture transform or the accel udev rule.
- **Report** — `--report` prints a five-layer dump for bug reports.

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
| raytrektab RT08WT | CachyOS Plasma Wayland | Same home tuple. Tilt: `-1, 0, 0; 0, 1, 0; 0, 0, -1` (`9/10`) on `KIOX000A` (poll; ring buffer unused) |
| Acer Chromebook Tab 510 (Trogdor) | postmarketOS Plasma Mobile | Tilt: wiki `0, 1, 0; -1, 0, 0; 0, 0, -1` (`5/10`) on `cros-ec-accel` |
| Acer Chromebook Tab 10 | Debian 13 XFCE X11 | `T=Normal`, `R_touch=0`, `R_pen=0` |

The Plasma W620 seed is not applied on GNOME. The same DMI can need a different home tuple under a different compositor.

## Build and install

See [docs/build.md](docs/build.md) for host packages and packaging.

On the machine that will run TiltBack:

```sh
./scripts/deps.sh
./scripts/build.sh
# packages land in dist/  (.rpm / .deb / .pkg.tar.zst)
sudo dnf install ./dist/tiltback-*.rpm          # Fedora / openSUSE: zypper
# sudo apt install ./dist/tiltback_*.deb        # Debian / Ubuntu
# sudo pacman -U dist/tiltback-*.pkg.tar.zst    # Arch
```

That installs to `/usr` and enables greeter persist (`tiltback-greeter.service`) plus the Tilt helper (`tiltback-accel.path`). Launch `/usr/bin/tiltback` from the application menu. A leftover `~/.local` desktop file wins over the packaged one — close any open window and launch from the menu again.

On GNOME also install `tiltback-gnome` (passwordless HID rebind). Alpine and Fedora/openSUSE can pull it when GNOME is present; Debian and Arch need the second package named.

`./scripts/build.sh --local` still installs a session-only clinic to `~/.local`. It does **not** fix the login OSK.

Cross-building for postmarketOS from a glibc PC:

```sh
./scripts/build-apk.sh
# then on the tablet:
sudo apk add --allow-untrusted ./tiltback-*.apk ./tiltback-gnome-*.apk
```

QML is interpreted (`NO_CACHEGEN`) so an Alpine 3.22 (Qt 6.8) build can load on postmarketOS 26.06 (Qt 6.11).

## Using the clinic

1. Open TiltBack on the graphical session (not a headless SSH without the session bus).
2. Apply **Picture** until the panel is upright. Keep or revert.
3. Apply **Finger** and **Pen** until a tap and a stylus contact land on the pixel they appear to belong to.
4. If auto-rotate picks the wrong `T` when you tip the chassis, use **Tilt**. Hold a landscape or portrait, then Prev / Next (or Solve after four bezel-down captures) until the live enum and `T` match that hold. Keep the sensor reload. Packaged installs enable `tiltback-accel.path` so the first apply is one Polkit prompt; later applies are passwordless. CLI: `tiltback --apply-tilt next|prev|0-9|identity|wiki`, `--capture-tilt bottom|right|top|left`, `--solve-tilt`, `--revert-tilt`.
5. **Save home**.
6. **Install follow** in the dashboard (or `tiltback --install-follow`). The GUI process must not also run `--follow`.
7. **Save home** also copies that tuple into the greeter (GDM / SDDM / Plasma Login Manager) when the `/usr` package is installed. The first unlock is still unfixed; the next logout or reboot should match. On Plasma Login, picture comes from the copied `kwinoutputconfig.json`; finger and pen need greeter `--follow` because KWin zeros `Orientation` when it applies T. The accel rule is system-wide and does not need a greeter copy. See [docs/greeter-and-boot.md](docs/greeter-and-boot.md).

Other entry points: `tiltback --report`, `tiltback --save-home`. Never `pkill -f tiltback` (it matches SSH).

## Documentation

| Document | Contents |
| --- | --- |
| [docs/build.md](docs/build.md) | Host packages, install prefixes, follow, Alpine apk, GNOME helper |
| [docs/concept-and-feasibility.md](docs/concept-and-feasibility.md) | Model, four layers, current state, what is not shipped |
| [docs/home-offset-and-follow.md](docs/home-offset-and-follow.md) | Home tuple and why follow exists |
| [docs/case-galaxy-book-w620.md](docs/case-galaxy-book-w620.md) | First clinic case (W620 on Plasma) |
| [docs/case-rt08wt.md](docs/case-rt08wt.md) | raytrektab RT08WT: poll `KIOX000A`, dead buffer, mount matrix `9/10` |
| [docs/case-trogdor-tab510.md](docs/case-trogdor-tab510.md) | Acer Chromebook Tab 510: `cros-ec-accel` poll, wiki matrix `5/10` |
| [docs/w620-runbook.md](docs/w620-runbook.md) | Measured W620 values and how to re-apply |
| [docs/greeter-and-boot.md](docs/greeter-and-boot.md) | Login screen vs Plymouth; why greeter copy does not replace the clinic |

## License

Copyright 2026 The TiltBack authors.

TiltBack is free software: you can redistribute it and/or modify it under the terms of the [GNU General Public License](LICENSE), version 3 or (at your option) any later version.
