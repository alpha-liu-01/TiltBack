# Building TiltBack

KWin (Plasma Wayland) is the first pick when that session bus is present. Mutter / GNOME Wayland is always compiled (session bus `org.gnome.Mutter.DisplayConfig`). An X11 RandR + XInput backend is compiled when `libx11`, `libxrandr`, and `libxi` are present. Missing those packages is not a hard fail: Alpine musl / a Wayland-only host still builds KWin + Mutter. Phosh/phoc is not a backend.

Two host paths. Do not mix their outputs: a glibc binary will not run on postmarketOS musl, and the Alpine musl binary is for pmOS.

| Path | When | Command |
| --- | --- | --- |
| Host glibc (or native musl) | Debian, Fedora, Arch, openSUSE, or Alpine **on the machine that will run it** | `./scripts/deps.sh` then `./scripts/build.sh --install` |
| Alpine 3.22 musl Docker | Binary copy for postmarketOS from a glibc PC | `./scripts/build-alpine.sh` |
| Alpine 3.22 musl Docker | `.apk` for postmarketOS (`tiltback` + `tiltback-gnome`) | `./scripts/build-apk.sh` |

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

X11 / XFCE (example): `DISPLAY=:0`, `XAUTHORITY` from the session, `XDG_RUNTIME_DIR=/run/user/$(id -u)`, and the session bus.

Never `pkill -f tiltback` (it matches SSH).

GNOME leftover residual is a udev matrix plus a HID unbind/bind. Mutter keeps evdev open, so `udevadm trigger` is not the apply. From a `~/.local` install that is `pkexec` of `~/.config/tiltback/rebind-hid.sh`. After `tiltback-gnome` is installed, the GUI and follow write `/run/tiltback/rebind-request` and the system path unit runs `/usr/libexec/tiltback/rebind-hid.sh` as root — no password if that unit is enabled.

GNOME’s app menu prefers `~/.local/share/applications` over `/usr/share`. A leftover clinic desktop with `Exec=…/.local/bin/tiltback` keeps launching the local binary (and that `pkexec`) after an apk install. Close any open TiltBack window and launch from the menu again. Logout is not required.

## Follow

```sh
tiltback --install-follow
systemctl --user enable --now tiltback-follow.service
```

`--install-follow` writes `/usr/bin/tiltback --follow` when that file exists, otherwise the binary that ran the command (`~/.local/bin/tiltback` on a prefix install). Follow restamps residuals only. The GUI process must not also run `--follow`.

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
| `tiltback` | `/usr/bin/tiltback`, `org.tiltback.TiltBack.desktop`, hicolor icons | `qt6-qtbase` `qt6-qtdeclarative` `qt6-qtwayland` |
| `tiltback-gnome` | `/usr/libexec/tiltback/rebind-hid.sh`, `tiltback-rebind.path` / `.service`, `80-tiltback.preset`, tmpfiles.d | pulled by `install_if` |

`tiltback-gnome` uses Alpine `install_if="tiltback gnome-shell systemd"`. `apk add tiltback` on a GNOME + systemd machine (postmarketOS GNOME) pulls the helper. Post-install creates `/run/tiltback` (tmpfiles `1777`), enables `tiltback-rebind.path`, and ships `80-tiltback.preset` (`enable tiltback-rebind.path`) so `postmarketos-base-systemd`’s `disable *` preset does not undo it. That is the packaged form of:

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

Other distros can do the same split:

| Family | How the GNOME helper attaches | Enable the path unit |
| --- | --- | --- |
| Alpine / pmOS apk | `install_if` on `gnome-shell` + `systemd` | `tiltback-gnome.post-install` |
| Debian / Ubuntu | `tiltback-gnome` with `Recommends:` / `Enhances: gnome-shell` | `packaging/debian/tiltback-gnome.postinst` (`deb-systemd-helper`) |
| Fedora / openSUSE | `%package gnome` + `Supplements: (tiltback and gnome-shell)` | `%post gnome` + `%systemd_post` |
| Arch | `optdepends=('gnome-shell: Mutter HID rebind')` | `.install` `post_install()` |

The udev symlink (`/etc/udev/rules.d/61-tiltback.rules` → `~/.config/tiltback/61-tiltback.rules`) is still per-user and is not created by the package: the package does not know which home to point at. The clinic prints that `ln -sf` if the link is missing.

On the tablet, after the apks are copied (`packaging/alpine/packages/<arch>/`):

```sh
sudo apk add --allow-untrusted ./tiltback-*.apk ./tiltback-gnome-*.apk
# GUI and follow: /usr/bin/tiltback (or the GNOME apps menu)
# leftover ~/.local desktop/binary are renamed or removed by post-install
# close an already-open TiltBack window and launch from the menu; no logout
```
