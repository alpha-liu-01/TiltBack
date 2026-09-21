# Greeter and boot logo (W620)

The session clinic does not run until after you unlock the machine. This note is the layer *before* that: the display manager (password / OSK) and the boot splash. It does not replace [home-offset-and-follow.md](home-offset-and-follow.md) or the compositor backends.

Probed chassis: Samsung Galaxy Book 10.6 (SM-W620). Kernel DRM `panel-orientation` is already `RIGHT_UP` (3). That hint is live on postmarketOS and on Fedora. Userspace still needs a different landscape for this hold (Plasma `Rotated90` / KScreen `left`, or GNOME `Rotated180`) plus a digitizer residual.

## Three clocks, not one bug

| When | Who paints | Who owns T | Who owns touch / pen |
| --- | --- | --- | --- |
| Boot splash | Plymouth (DRM/KMS) | Kernel `panel-orientation` / `video=` | nobody (no libinput) |
| Login / OSK | GDM, SDDM, or Plasma Login Manager | that greeter’s compositor config | udev at coldplug, then that compositor |
| After unlock | your session | `~/.config/monitors.xml` or `kwinoutputconfig.json` | follow + udev / KWin `orientationDBus` |

`tiltback-follow.service` is a **user** unit (`After=graphical-session.target`). It cannot run on the greeter and it cannot run in Plymouth.

## Greeter persist (shipped)

`/usr/libexec/tiltback/install-greeter.sh` copies the newest `/home/*/.config/tiltback/home.json` clinic into the greeter that is actually installed. The main `tiltback` package enables `tiltback-greeter.service` (`Before=display-manager.service`) and `tiltback-greeter.path` (`/run/tiltback/greeter-request`). Save home, persistent Keep, and `tiltback --install-greeter` touch that request file.

| Greeter home | Owner (probed) | Picture | Residual |
| --- | --- | --- | --- |
| `/var/lib/plasmalogin` | `plasmalogin` (Fedora 44 Plasma Mobile, `plasmalogin.service`) | `kwinoutputconfig.json` | `kcminputrc` plus greeter `--follow` |
| `/var/lib/sddm` | `sddm` | same | same files; no Plasma Login follow hook |
| `/var/lib/gdm` | directory owner (`gdm`) | `monitors.xml` | udev file if the clinic user has one |
| `/var/lib/gdm3` | directory owner (`gdm`) | same | same |

On Fedora, copies `chown` the greeter user and `restorecon -F` (`xdm_var_lib_t` vs `config_home_t`). A leftover udev rule is materialized as a **real file** at `/etc/udev/rules.d/61-tiltback.rules` (not a symlink into home). The script does **not** HID-rebind: the greeter has not opened evdev yet.

On the W620 GNOME home (`T=Rotated180`, `R_touch=8`) Mutter composes T onto touch **and** you still need constant R=8. T-only on the greeter leaves the OSK 180° off the keys. R-only can make taps hit an upside-down keyboard. Both are required to type a password.

On Plasma, KWin zeros digitizer `Orientation` on every T change — the same wipe that makes session follow mandatory. Copying `kcminputrc` into `plasmalogin` is not enough: greeter KWin applies the copied `Rotated90` and writes `R=0`. A udev leftover would be overwritten the same way. The installer also copies `home.json` and the package ships `tiltback-greeter-follow.service` plus a `plasma-login-wayland.target.d` drop-in (`Wants=`) so `/usr/bin/tiltback --follow` runs as `plasmalogin` after greeter KWin is on the bus and restamps `orientationDBus`. Fedora systemd 258 ignores a regular file in `*.wants/` and rpm rewrites a repo symlink; the drop-in is the enable hook. That is the session clinic on the greeter user, not a second engine.

A `~/.local` install cannot chown the greeter. Use a `/usr` package (or `PREFIX=/usr`) and one `sudo` to enable the units.

## Does that fix the boot logo?

**No.** Not on postmarketOS, and not on Fedora 44.

Plymouth starts in the initramfs, modesets the panel, and exits before the display manager. It does not read `monitors.xml`, `kwinoutputconfig.json`, `kcminputrc`, `home.json`, or `LIBINPUT_CALIBRATION_MATRIX`. A unit `Before=display-manager.service` runs *after* the splash has already been shown.

Fedora and postmarketOS both use Plymouth. The theme (pmOS mark vs Fedora) does not change which transform it honors: DRM `panel-orientation` (and `video=…panel_orientation=` if set).

There is no input stack on the splash, so “match the logo to a finger” is not a Plymouth problem.

## Would a kernel T replace the clinic?

A `video=eDP-1:panel_orientation=left_side_up` (or a `LEFT_UP` quirk instead of the current `RIGHT_UP`) is the only lever that can rotate Plymouth. It is **system** height in [concept-and-feasibility.md](concept-and-feasibility.md): reboot, root, and it redefines what every compositor calls “normal.”

It is not an objectively better replacement for the work already shipped.

1. **The kernel quirk is already live and still wrong for this hold.** That was the first W620 result. Userspace T exists *because* `RIGHT_UP` + compositor default is the other landscape.

2. **Changing the hint does not remove residuals.** Plasma still needs `R=8` on both digitizers and follow (KWin zeros R). GNOME still needs constant touch udev R=8 and a composed pen `R(T)` plus HID rebind. Plymouth never applies those.

3. **Stored T would fight the new hint.** Session `monitors.xml` / `kwinoutputconfig.json` still apply on top of the new “normal.” Homes already measured (`Rotated90` + `R=8/8` on Plasma, `Rotated180` + `R=8/2` on GNOME) would have to be re-cliniced. Greeter copies of those files would be stale until rewritten.

4. **The greeter is still a different user.** Even with an upright KMS hint, GDM / SDDM / `plasmalogin` can load an empty or opposite output config and put T back. Kernel T is not a greeter persist.

5. **Follow stays.** Display Configuration revert, logout/login, and pose changes still drop or skip R. A cmdline snippet does not watch `MonitorsChanged` or KWin `PropertiesChanged`.

Kernel T is a **Plymouth-only** extra (and a nicer default getty / early KMS). It is Phase 7 export territory: optional, next to the clinic, not instead of it. The greeter copy is a **login/OSK** extra. Neither substitutes for save-home + follow.

## Fedora 44 Plasma Mobile W620 (probed)

`alpha@fedora`, uid 1000, `plasmalogin` enabled. Session home already measured: `T=Rotated90`, `R_touch=8`, `R_pen=8`. Greeter home `/var/lib/plasmalogin` starts empty (`750`, `xdm_var_lib_t`). After `sudo /usr/libexec/tiltback/install-greeter.sh`, expect `.config/kwinoutputconfig.json`, `.config/kcminputrc`, and `.config/tiltback/home.json` owned by `plasmalogin`. Picture-only (T copied, R wiped) was probed: OSK picture upright, finger and pen still inverted. The greeter follow unit is the residual path. Do not reuse the GNOME `Rotated180` / `R=8/2` seed. The Fedora boot logo will stay `RIGHT_UP` until someone exports a `video=` / quirk snippet.

## Related

- [case-galaxy-book-w620.md](case-galaxy-book-w620.md) — first Plasma clinic
- [w620-runbook.md](w620-runbook.md) — Plasma home values
- [concept-and-feasibility.md](concept-and-feasibility.md) — four layers; persist heights
- [home-offset-and-follow.md](home-offset-and-follow.md) — why follow exists after persist
