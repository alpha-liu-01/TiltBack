# Building TiltBack

KWin (Plasma Wayland) is the first pick when that session bus is present. Mutter / GNOME Wayland is always compiled (session bus `org.gnome.Mutter.DisplayConfig`). An X11 RandR + XInput backend is compiled when `libx11`, `libxrandr`, and `libxi` are present. Missing those packages is not a hard fail: Alpine musl / a Wayland-only host still builds KWin + Mutter. Phosh/phoc is not a backend.

Two host paths. Do not mix their outputs: a glibc binary will not run on postmarketOS musl, and the Alpine musl binary is for pmOS.

| Path | When | Command |
| --- | --- | --- |
| Host glibc (or native musl) | Debian, Fedora, Arch, openSUSE, or Alpine **on the machine that will run it** | `./scripts/deps.sh` then `./scripts/build.sh --install` |
| Alpine 3.22 musl Docker | Cross-build for postmarketOS from a glibc PC | `./scripts/build-alpine.sh` |

QML is interpreted (`NO_CACHEGEN`). Alpine 3.22 ships Qt 6.8; postmarketOS 26.06 is Qt 6.11. A 6.8 qmlcache will not load on 6.11.

## Host packages

`./scripts/deps.sh` prints the one-liner for this distro and checks cmake, a C++ compiler, pkg-config, libdrm, and Qt 6 Quick + QuickControls2 + DBus. It does not run sudo.

| Family | Install |
| --- | --- |
| Debian / Ubuntu | `sudo apt install cmake ninja-build g++ pkg-config libdrm-dev qt6-base-dev qt6-declarative-dev libx11-dev libxrandr-dev libxi-dev` |
| Fedora | `sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config libdrm-devel qt6-qtbase-devel qt6-qtdeclarative-devel libX11-devel libXrandr-devel libXi-devel` |
| Arch | `sudo pacman -S --needed cmake ninja gcc pkgconf libdrm qt6-base qt6-declarative libx11 libxrandr libxi` |
| Alpine (native) | `sudo apk add cmake ninja g++ pkgconf libdrm-dev qt6-qtbase-dev qt6-qtdeclarative-dev` — X11 optional: `libx11-dev libxrandr-dev libxi-dev` |
| openSUSE | `sudo zypper install cmake ninja gcc-c++ pkgconf-pkg-config libdrm-devel qt6-base-devel qt6-declarative-devel libX11-devel libXrandr-devel libXi-devel` |

X11 session packages are recommended so CMake defines `TILTBACK_X11`. A Wayland-only or Alpine musl build without them still produces a KWin + Mutter clinic.

Ninja is preferred. Without it, `./scripts/build.sh` uses Unix Makefiles.

## Install and run

Default prefix is `~/.local` (no root). `PREFIX` overrides it.

```sh
./scripts/deps.sh
./scripts/build.sh --install
# binary:  ~/.local/bin/tiltback
# desktop: ~/.local/share/applications/org.tiltback.TiltBack.desktop
# icons:   ~/.local/share/icons/hicolor/*/apps/org.tiltback.TiltBack.png
```

If `PREFIX/bin` is not on `PATH`, the installed desktop file gets an absolute `Exec=`. Launch from the Plasma app menu or:

Plasma or GNOME Wayland:

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
systemd-run --user \
  -E WAYLAND_DISPLAY -E XDG_RUNTIME_DIR -E DBUS_SESSION_BUS_ADDRESS \
  "$HOME/.local/bin/tiltback"
```

X11 / XFCE (example): `DISPLAY=:0`, `XAUTHORITY` from the session, `XDG_RUNTIME_DIR=/run/user/$(id -u)`, and the session bus. Never `pkill -f tiltback`.

Never `pkill -f tiltback` (it matches SSH).

## Follow

```sh
~/.local/bin/tiltback --install-follow
systemctl --user enable --now tiltback-follow.service
```

Follow restamps residuals only. The GUI process must not also run `--follow`.

The RO-home `/tmp` + runtime systemd drop-in path is a postmarketOS emergency, not the default. A writable `~/.config/systemd/user` is enough.

## postmarketOS from a fast PC

```sh
./scripts/build-alpine.sh
scp build-alpine/tiltback data/org.tiltback.TiltBack.desktop user@tablet:
```

On the tablet: `~/.local/bin/tiltback` (or `/tmp/tiltback` if home is emergency-RO) and the desktop file under `~/.local/share/applications/` with a full `Exec=` path.

## Alpine / postmarketOS packages

`./scripts/build-apk.sh` builds two musl `.apk` files with the same `org.tiltback.TiltBack` hicolor icon the desktop file already uses:

| Package | Contents |
| --- | --- |
| `tiltback` | `/usr/bin/tiltback`, `org.tiltback.TiltBack.desktop`, hicolor icons |
| `tiltback-gnome` | `/usr/libexec/tiltback/rebind-hid.sh`, `tiltback-rebind.path` / `.service`, tmpfiles.d |

`tiltback-gnome` uses Alpine `install_if="tiltback gnome-shell systemd"`. `apk add tiltback` on a GNOME + systemd machine (postmarketOS GNOME) pulls the helper. Post-install enables `tiltback-rebind.path` and ships `80-tiltback.preset` (`enable tiltback-rebind.path`) so `postmarketos-base-systemd`'s `disable *` preset does not undo it. That is the packaged form of:

```sh
sudo cp ~/.config/tiltback/tiltback-rebind.service /etc/systemd/system/
sudo cp ~/.config/tiltback/tiltback-rebind.path /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tiltback-rebind.path
```

The packaged path watches `/run/tiltback/rebind-request` (world-writable after tmpfiles). Follow writes the user udev rule, touches that request file, and the system unit rebinds the HID devices as root. Do **not** put a home path in a packaged unit.

Other distros can do the same split:

| Family | How the GNOME helper attaches | Enable the path unit |
| --- | --- | --- |
| Alpine / pmOS apk | `install_if` on `gnome-shell` + `systemd` | `tiltback-gnome.post-install` |
| Debian / Ubuntu | `tiltback-gnome` with `Recommends:` / `Enhances: gnome-shell` | `packaging/debian/tiltback-gnome.postinst` (`deb-systemd-helper`) |
| Fedora / openSUSE | `%package gnome` + `Supplements: (tiltback and gnome-shell)` | `%post gnome` + `%systemd_post` |
| Arch | `optdepends=('gnome-shell: Mutter HID rebind')` | `.install` `post_install()` |

The udev symlink (`/etc/udev/rules.d/61-tiltback.rules` → the user rule file) is still per-user and is not created by the package: the package does not know which home to point at. The clinic prints that `ln -sf` if the link is missing.

On the tablet, after the apks are copied:

```sh
sudo apk add --allow-untrusted ./tiltback-0.0.0-r1.apk ./tiltback-gnome-0.0.0-r1.apk
# follow and the GUI must be /usr/bin/tiltback, not a leftover ~/.local/bin copy
```
