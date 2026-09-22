# Building TiltBack

KWin (Plasma Wayland) is the first pick when that session bus is present. Mutter / GNOME Wayland is always compiled (session bus `org.gnome.Mutter.DisplayConfig`). An X11 RandR + XInput backend is compiled when `libx11`, `libxrandr`, and `libxi` are present. Missing those packages is not a hard fail: Alpine musl / a Wayland-only host still builds KWin + Mutter. Phosh/phoc is not a backend.

Two host paths. Do not mix their outputs: a glibc binary will not run on postmarketOS musl, and the Alpine musl binary is for pmOS.

| Path | When | Command |
| --- | --- | --- |
| Host glibc (or native musl) | Debian, Fedora, Arch, openSUSE **on the machine that will run it** | `./scripts/deps.sh` then `./scripts/build.sh` → `dist/*.deb` / `*.rpm` / `*.pkg.tar.zst` |
| Session-only (no greeter) | No root, clinic this login only | `./scripts/build.sh --local` → `~/.local` |
| Alpine 3.22 musl Docker | Binary copy for postmarketOS from a glibc PC | `./scripts/build-alpine.sh` |
| Alpine 3.22 musl Docker | `.apk` for postmarketOS (`tiltback` + `tiltback-gnome`) | `./scripts/build-apk.sh` (or `./scripts/build.sh` on native Alpine) |

QML is interpreted (`NO_CACHEGEN`). Alpine 3.22 ships Qt 6.8; postmarketOS 26.06 is Qt 6.11. A 6.8 qmlcache will not load on 6.11.

## Host packages

`./scripts/deps.sh` prints the one-liner for this distro and checks cmake, a C++ compiler, pkg-config, libdrm, and Qt 6 Quick + QuickControls2 + DBus. It does not run sudo.

| Family | Install |
| --- | --- |
| Debian / Ubuntu | `sudo apt install cmake ninja-build g++ pkg-config libdrm-dev qt6-base-dev qt6-declarative-dev libx11-dev libxrandr-dev libxi-dev dpkg-dev debhelper` |
| Fedora | `sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config libdrm-devel qt6-qtbase-devel qt6-qtdeclarative-devel libX11-devel libXrandr-devel libXi-devel rpm-build` |
| Arch | `sudo pacman -S --needed cmake ninja gcc pkgconf libdrm qt6-base qt6-declarative libx11 libxrandr libxi base-devel` |
| Alpine (native) | `sudo apk add cmake ninja g++ pkgconf libdrm-dev qt6-qtbase-dev qt6-qtdeclarative-dev` — X11 optional: `libx11-dev libxrandr-dev libxi-dev` |
| openSUSE | `sudo zypper install cmake ninja gcc-c++ pkgconf-pkg-config libdrm-devel qt6-base-devel qt6-declarative-devel libX11-devel libXrandr-devel libXi-devel rpm-build` |

X11 session packages are recommended so CMake defines `TILTBACK_X11`. A Wayland-only or Alpine musl build without them still produces a KWin + Mutter clinic.

Ninja is preferred. Without it, `./scripts/build.sh` uses Unix Makefiles.

## Install and run

`./scripts/build.sh` writes packages under `dist/`. Install those so the binary and greeter units land in `/usr`.

```sh
./scripts/deps.sh
./scripts/build.sh
sudo dnf install ./dist/tiltback-*.rpm                 # Fedora
# sudo zypper install ./dist/tiltback-*.rpm            # openSUSE
# sudo apt install ./dist/tiltback_*.deb               # Debian / Ubuntu
# sudo pacman -U dist/tiltback-*.pkg.tar.zst           # Arch
```

On GNOME also install `tiltback-gnome` from the same `dist/` directory.

`./scripts/build.sh --local` (or `PREFIX=…`) is a no-root session clinic only. Greeter persist will not run from `~/.local`.

Launch `/usr/bin/tiltback` from the application menu, or:

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
systemd-run --user \
  -E WAYLAND_DISPLAY -E XDG_RUNTIME_DIR -E DBUS_SESSION_BUS_ADDRESS \
  /usr/bin/tiltback
```

X11 / XFCE (example): `DISPLAY=:0`, `XAUTHORITY` from the session, `XDG_RUNTIME_DIR=/run/user/$(id -u)`, and the session bus.

Never `pkill -f tiltback` (it matches SSH).

GNOME leftover residual is a udev matrix plus a HID unbind/bind. Mutter keeps evdev open, so `udevadm trigger` is not the apply. After `tiltback-gnome` is installed, the GUI and follow write `/run/tiltback/rebind-request` and the system path unit runs `/usr/libexec/tiltback/rebind-hid.sh` as root — no password if that unit is enabled.

GNOME’s app menu prefers `~/.local/share/applications` over `/usr/share`. A leftover clinic desktop with `Exec=…/.local/bin/tiltback` keeps launching the local binary. Close any open TiltBack window and launch from the menu again. Logout is not required.

## Follow

```sh
tiltback --install-follow
systemctl --user enable --now tiltback-follow.service
```

`--install-follow` writes `/usr/bin/tiltback --follow` when that file exists, otherwise the binary that ran the command (`~/.local/bin/tiltback` on a prefix install). Follow restamps residuals only. The GUI process must not also run `--follow`. `tiltback --install-greeter` updates the login-screen copy (needs the `/usr` helper and `tiltback-greeter.path`).

The RO-home `/tmp` + runtime systemd drop-in path is a postmarketOS emergency, not the default. A writable `~/.config/systemd/user` is enough.

## postmarketOS from a fast PC

Binary-only copy (KWin clinic, or a one-off before packaging):

```sh
./scripts/build-alpine.sh
scp build-alpine/tiltback data/org.tiltback.TiltBack.desktop user@tablet:
```

On the tablet: runtime Qt (`sudo apk add qt6-qtbase qt6-qtdeclarative qt6-qtwayland`), then `~/.local/bin/tiltback` (or `/tmp/tiltback` if home is emergency-RO) and the desktop file under `~/.local/share/applications/` with a full `Exec=` path.

On GNOME that local desktop will steal the app menu from a later `/usr` apk. Prefer `./scripts/build-apk.sh` on GNOME, or let the apk post-install rename the leftover binary and remove that desktop (see below).

## Alpine / postmarketOS packages

`./scripts/build-apk.sh` packs the **working tree** (not `git archive`) into two musl `.apk` files under `packaging/alpine/packages/`. Same `org.tiltback.TiltBack` hicolor icon the desktop file already uses. `pkgrel` is in `packaging/alpine/APKBUILD`.

| Package | Contents | Runtime |
| --- | --- | --- |
| `tiltback` | `/usr/bin/tiltback`, desktop + icons, `/usr/libexec/tiltback/install-greeter.sh`, `/usr/libexec/tiltback/apply-accel.sh`, `tiltback-greeter.service` / `.path`, `tiltback-accel.path` / `.service`, `tiltback-greeter-follow.service` plus `plasma-login-wayland.target.d` drop-in, `80-tiltback.preset`, tmpfiles.d | `qt6-qtbase` `qt6-qtdeclarative` `qt6-qtwayland` |
| `tiltback-gnome` | `/usr/libexec/tiltback/rebind-hid.sh`, `tiltback-rebind.path` / `.service`, `81-tiltback-gnome.preset` | pulled by `install_if` |

The main package post-install creates `/run/tiltback` (tmpfiles `1777`) and enables `tiltback-greeter.service` / `.path` and `tiltback-accel.path` (`80-tiltback.preset`). Greeter: the login OSK gets the last clinic T/R before the display manager starts. See [greeter-and-boot.md](greeter-and-boot.md). `tiltback --install-greeter` and Save home touch `/run/tiltback/greeter-request`. Tilt apply: the GUI or `tiltback --apply-tilt next|prev|0-7|identity|wiki` writes `/run/tiltback/accel-request`; the path unit runs `apply-accel.sh` as root (one well-formed `61-tiltback-accel.rules`, udev reload, `iio-sensor-proxy` restart). `--revert-tilt` restores the pre-cycle matrix (remove our file, or re-apply the kept 3×3). Save home records `accelMountMatrix` in `home.json`; follow does not apply it. Do not put a home path in the packaged accel unit.

`tiltback-gnome` uses Alpine `install_if="tiltback gnome-shell systemd"`. `apk add tiltback` on a GNOME + systemd machine (postmarketOS GNOME) pulls the helper. Post-install enables `tiltback-rebind.path` and ships `81-tiltback-gnome.preset` (`enable tiltback-rebind.path`) so `postmarketos-base-systemd`’s `disable *` preset does not undo it. That is the packaged form of:

```sh
sudo cp ~/.config/tiltback/tiltback-rebind.service /etc/systemd/system/
sudo cp ~/.config/tiltback/tiltback-rebind.path /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tiltback-rebind.path
```

The packaged path watches `/run/tiltback/rebind-request` (world-writable after tmpfiles). Follow writes the user udev rule, touches that request file, and the system unit rebinds the HID devices as root. Do **not** put a home path in a packaged unit. The binary does not `pkexec` the user script when `/usr/libexec/tiltback/rebind-hid.sh` is present.

`tiltback` post-install / post-upgrade also clears a leftover clinic install so the GNOME menu and follow unit cannot keep using `~/.local`:

- remove `~/.local/share/applications/org.tiltback.TiltBack.desktop` if `Exec` points at `.local/bin/tiltback`
- rename `~/.local/bin/tiltback` to `tiltback.pre-apk`
- rewrite `~/.config/systemd/user/tiltback-follow.service` `ExecStart` to `/usr/bin/tiltback`

Every family ships greeter persist on the **main** package (`install-greeter.sh` + `tiltback-greeter.*`). The GNOME leftover rebind stays in `tiltback-gnome`. The main package also clears a leftover `~/.local` clinic desktop / binary (renamed `tiltback.pre-apk` on Alpine, `tiltback.pre-pkg` elsewhere) and rewrites follow `ExecStart` to `/usr/bin/tiltback`.

| Family | How the GNOME helper attaches | Enable the path unit | Build |
| --- | --- | --- | --- |
| Alpine / pmOS apk | `install_if` on `gnome-shell` + `systemd` | `tiltback-gnome.post-install` | `./scripts/build-apk.sh` |
| Debian / Ubuntu | `tiltback-gnome` (`Suggests:` from `tiltback`; `Enhances: gnome-shell`) | `packaging/debian/tiltback-gnome.postinst` | `./scripts/build.sh` → `dist/*.deb` |
| Fedora / openSUSE | `%package gnome` + `Supplements: (tiltback and gnome-shell)` | `%post gnome` + `%systemd_post` | `./scripts/build.sh` → `dist/*.rpm` |
| Arch | `optdepends=('tiltback-gnome: …')` on `tiltback` | `packaging/arch/tiltback.install` | `./scripts/build.sh` → `dist/*.pkg.tar.zst` |

On GNOME, install **both** `tiltback` and `tiltback-gnome`. Alpine pulls the helper via `install_if`. Fedora/openSUSE may pull it via `Supplements`. Debian and Arch do not: `sudo apt install tiltback tiltback-gnome` or `pacman -U tiltback-*.pkg.tar.zst tiltback-gnome-*.pkg.tar.zst`.

The udev symlink (`/etc/udev/rules.d/61-tiltback.rules` → `~/.config/tiltback/61-tiltback.rules`) is still per-user and is not created by the package: the package does not know which home to point at. The clinic prints that `ln -sf` if the link is missing.

On the tablet, after the apks are copied (`packaging/alpine/packages/<arch>/`):

```sh
sudo apk add --allow-untrusted ./tiltback-*.apk ./tiltback-gnome-*.apk
# GUI and follow: /usr/bin/tiltback (or the GNOME apps menu)
# leftover ~/.local desktop/binary are renamed or removed by post-install
# close an already-open TiltBack window and launch from the menu; no logout
```
