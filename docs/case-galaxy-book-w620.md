# Case 1: Samsung Galaxy Book 10.6 (SM-W620)

Probed over SSH on 2026-09-21 from a running postmarketOS v26.06 session. This is the first real TiltBack entry point: one wrong layer, two correct layers, on Plasma 6 Wayland.

## What the user sees

- Display picture is **upside down** relative to how the chassis is held (type cover attached).
- Finger touch and Wacom stylus are **the right way up**.
- Result: a tap or pen contact does not land on the pixel it appears to belong to.

This is the “picture disagrees with the sensors” case from the concept note, not the four-way nightmare and not the inverted-cursor case.

## Machine

| Field | Value |
| --- | --- |
| DMI vendor | `SAMSUNG ELECTRONICS CO., LTD.` |
| DMI product | `Galaxy Book 10.6` |
| DMI version / board | `P03HAD` / `SM-W620NZKBXAC` |
| Chassis type | 32 (tablet) |
| BIOS | `P03HAD.003.171122.WY.2239` (2017-11-22) |
| CPU / GPU | Intel Core m3-7Y30 / i915 `8086:591E` (HD 615) |
| OS | postmarketOS v26.06, kernel `6.18.52-0-lts` |
| Session | Plasma 6.6.6, KWin 6.6.6, Wayland (`wayland-0`) |
| Seat | type cover attached; `tabletModeAvailable=true`, `tabletModeEngaged=false` |

## Four layers, as they actually are

### Picture

- Connector `eDP-1`, DRM id 106. **No EDID** (`edid` sysfs is 0 bytes; KWin log: `Could not find edid for connector`). Physical size reported as 0×0 mm.
- Native mode is **1280×1920@60** — a portrait panel used in a landscape chassis.
- DRM `panel orientation` property is **3**. In the kernel that is `DRM_MODE_PANEL_ORIENTATION_RIGHT_UP`.
- That value is not a guess. This DMI is in `drm_panel_orientation_quirks.c` since 2021: *“The Samsung Galaxy Book 10.6 uses a panel which has been mounted 90 degrees rotated.”* The quirk sets 1280×1920 + `RIGHT_UP`.
- KWin has persisted `"transform": "Rotated270"` in `~/.config/kwinoutputconfig.json`. KScreen reports `rotation: 8` (`right`) and a logical size of 1920×1280.
- Auto-rotation is `InTabletMode` only. There is **no IIO accelerometer** and `iio-sensor-proxy` is not on the bus, so nothing will “fix itself” when the cover comes off.

`Rotated270` is KWin’s usual translation of `RIGHT_UP` (compensate a panel whose native right edge is physically up). The kernel hint is live, KWin is honoring it, and the user still sees an upside-down picture. The “fundamental” fix already shipped. It is not sufficient.

### Finger

- `STMD1234:00 06CB:1058` (Synaptics, I2C), `ID_INPUT_TOUCHSCREEN`.
- Physical size from udev: 142×213 mm (portrait, same aspect as the panel).
- No `LIBINPUT_CALIBRATION_MATRIX`.
- KWin `InputDevice` `event10`: `touch=true`, `calibrationMatrix` identity, `rotation=0`, `supportsCalibrationMatrix=true`, `supportsRotation=true`, `outputName` empty.

### Pen

- `WCOM0028:00 2D1F:000C Stylus` (Wacom I2C), `ID_INPUT_TABLET`.
- Physical size from udev: 149×223 mm (also portrait).
- No `LIBINPUT_CALIBRATION_MATRIX`.
- KWin `InputDevice` `event9`: `tabletTool=true`, `calibrationMatrix` identity, `rotation=0`, `supportsCalibrationMatrix=true`, `supportsRotation=false`, `outputName` empty.

A companion `… Mouse` node exists on the same I2C device (`event8`). It is a relative pointer, not the digitizer.

### Arrow

Not reported as inverted on this machine. Not investigated further.

## What this proves about TiltBack

1. **Probe-first was the right call.** The interesting fact is not “Wayland is hard.” It is that a kernel quirk *and* a compositor transform are already in place, and the layers still disagree.
2. **Do not start with something more fundamental.** A new protocol, a new kernel quirk, or a udev matrix would be the wrong first code. This chassis already has the kernel quirk. The session-level lever is `kscreen-doctor`, and it works from a remote shell if `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, and `DBUS_SESSION_BUS_ADDRESS` are set to the graphical session.
3. **The Plasma 6 backend is real, not hypothetical.**
   - Display: `kscreen-doctor output.eDP-1.rotation.{none,left,right,inverted}` applies atomically and writes `kwinoutputconfig.json`.
   - Per-device residual: `org.kde.KWin.InputDevice` on `/org/kde/KWin/InputDevice/eventN` exposes read/write `calibrationMatrix`, `rotation`, and `orientationDBus`. Touch accepts a rotation or a matrix. The stylus accepts a matrix only.
4. **The clinic operation is two steps, not one.** Rotate the picture 180° (`right` → `left`), then apply a matching residual on finger and pen. Leaving inputs alone is how we reproduced the common “picture fixed, touch inverted” Galaxy Book report.
5. **Privilege is already the shape of the product.** The graphical user is in `video` (can read DRM, including `panel orientation`) but not `input` (`libinput list-devices` fails) and has no passwordless sudo. Diagnosis of display + KWin state does not need root. Raw evdev and udev writes do. The first tool should not.

## Session experiment (applied 2026-09-21)

1. `kscreen-doctor output.eDP-1.rotation.left` succeeded. KScreen rotation went `8` → `2`. KWin persisted `Rotated270` → `Rotated90`. Picture became upright.
2. Manual result: finger and pen **followed the display** and are now 180° wrong. The 180° gap between picture and sensors is a constant residual; rotating the output moves both layers together.
3. Compensating residual (applied next): KWin `orientationDBus=8` (`Qt::InvertedLandscapeOrientation`) on both `WCOM0028:00 2D1F:000C Stylus` and `STMD1234:00 06CB:1058`. Persisted in `~/.config/kcminputrc` as `Orientation=8`. Display left at `Rotated90`.
4. Manual result: picture, finger, and pen now agree for this landscape hold.

Manual confirmation: `R=8` on both digitizers is correct for **all four** Plasma rotations (none / left / inverted / right). The first “none breaks inputs” report was KWin wiping `R` to `0` on the transform change, not a portrait-specific residual.

Follow is still required: KWin zeros `Orientation` whenever `T` changes, including the 15s Display Configuration revert. The helper’s job on this chassis is to **re-stamp `R=8`**, not to pick a per-pose matrix.

Wrap-up and how to re-apply: [w620-runbook.md](w620-runbook.md). The live binary is `tiltback` (`--save-home`, `--install-follow`).

## Hypothesis this tests

If KWin composed `Rotated270` into touch and stylus, the picture and the sensors would be upside down *together*. The user would not report a mismatch. So the working theory is:

- Finger and pen are mapped in chassis / sensor space (identity residual).
- The picture is 180° from that space.
- Therefore `kscreen-doctor output.eDP-1.rotation.left` should upright the picture without a udev rule.

The failure mode to watch: KWin might start applying the new output transform to absolute devices, flipping the sensors with the picture. Then we would be in the state other Galaxy Book 10.6 Linux reports describe (picture fixed, touch inverted). That is still a TiltBack problem — it just means the first apply must be **picture + optional compensating matrix**, not picture alone.

Revert, if needed:

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
kscreen-doctor output.eDP-1.rotation.right
```

## Suggested first software, when we write any

Not a GUI. A read-only probe that prints this four-layer table on the machine that has the bug, then one action: “flip picture 180°” via the KWin/KScreen backend, with a revert countdown. If that loop works here, TiltBack has an entry-point backend and a first device profile. The QML wizard comes after the apply/revert path is boring.

## Open questions (not blocking the session fix)

- Upstream `RIGHT_UP` vs this hold: the kernel quirk is live and still implies the other landscape. Userspace overrides it; we did not patch the quirk.
- Missing EDID (`eDP-1-unknown`, size 0×0 mm).
- No IIO accelerometer; `autoRotation=InTabletMode` cannot help.
- Type cover touchpad “detected but dead” after a reattach (2026-09-21): KWin still had it enabled at `R=0`; USB instance had changed. Re-check after reboot with `tiltback --report`. Not part of the digitizer residual.
