# Case: Acer Chromebook Tab 510 (Google Trogdor)

Read-only SSH classify on 2026-09-22 from `user@10.0.0.119` (T0). The same chassis is now `user@10.0.0.117`. T0 only: no TiltBack install, no udev rewrite, wiki rule left in place.

This is the first **readable IMU** box we own. The wiki mount matrix is already on. T0 records **readable + already corrected**, and the identity T2 must write — not a new matrix hunt.

## Machine

| Field | Value |
| --- | --- |
| Hostname | `google-trogdor` |
| OS | postmarketOS v26.06, aarch64, kernel `6.18.26` |
| Session | Plasma Mobile, KWin 6.6.6 Wayland (`wayland-0`), uid `10000` |
| DT model | `Google Quackingstick (rev0+)` |
| Compatible | `google,quackingstick-sku1537` / `qcom,sc7180` |
| Product | Acer Chromebook Tab 510 / 7c gen2 |
| DMI `product_name` | empty — identify by DT, not DMI |
| Panel | `DSI-1` native `1200×1920@60`, scale 1.5 |
| Auto-rotation | `Always` in `kwinoutputconfig.json` |

No `~/.config/tiltback/`, no `tiltback` process.

## IIO

Four nodes. The panel accel is `iio:device2` / `cros-ec-accel` (`ATTR{label}=="accel-display"`), parent `platform:cros-ec-accel`. Sysfs raw is readable (`~30, -192, 16792` at rest; Z-dominant when the tablet is flat). There is **no** `in_accel_mount_matrix` in sysfs — the frame is udev-only. Do not compose a udev matrix on top of a kernel matrix here; the kernel file is empty.

| Node | `name` | Role |
| --- | --- | --- |
| `iio:device0` | `c440000.spmi:pmic@0:adc@3100` | PMIC ADC |
| `iio:device1` | `c440000.spmi:pmic@4:adc@3100` | PMIC ADC |
| `iio:device2` | `cros-ec-accel` | panel accel (`accel-display`) |
| `iio:device3` | `cros-ec-gyro` | gyro |

`IIO_SENSOR_PROXY_TYPE=iio-poll-accel` on the panel node (poll, not the RT08WT ring-buffer path). `/usr/lib/udev/rules.d/99-google-trogdor-accel.rules` forces poll when `in_accel_*_raw` exists (pmOS note: buffered mode fails on Duet 3).

## Wiki rule and leak

`/etc/udev/rules.d/61-cros-ec-accel.rules` is the [postmarketOS wiki](https://wiki.postmarketos.org) matrix:

```text
ENV{ACCEL_MOUNT_MATRIX}="0, 1, 0; -1, 0, 0; 0, 0, -1"
```

The file is meant to match `KERNEL=="iio*"` plus `ATTRS{modalias}=="platform:cros-ec-accel"`. It is written as four separate udev lines with **no** `\` continuations. `udevadm test` reports lines 3–4 “has no effect, ignoring”; the `ENV{ACCEL_MOUNT_MATRIX}=…` line then applies to **every** device. The same `ENV` is visible on the PMIC ADCs, `cros-ec-gyro`, DRM, and evdev — not only `cros-ec-accel`.

T2 must match **`cros-ec-accel` name/modalias** (or `label=accel-display`) on one well-formed rule. Do not copy the wiki’s `KERNEL=="iio*"` wrap.

## SensorProxy

`net.hadess.SensorProxy` on the **system** bus: `HasAccelerometer=true`. Rest enum `AccelerometerOrientation="left-up"` (matches persisted `T=Rotated90`).

```text
cros-ec-accel raw → udev ACCEL_MOUNT_MATRIX → iio-sensor-proxy poll → enum → KWin T
```

## Four-hold observation (2026-09-22)

SSH, no writes. Session: `XDG_RUNTIME_DIR=/run/user/10000`, user bus, `WAYLAND_DISPLAY=wayland-0`.

`kwinoutputconfig.json` `"transform"` stayed `Rotated90` while the tablet was tipped (file can rewrite without changing that key). Live `T` is `kscreen-doctor` `Rotation` (needs the Wayland display; without it the binary aborted). KScreen values: `1=none`, `2=left` / `Rotated90`, `4=inverted` / `Rotated180`, `8=right` / `Rotated270`.

All four SensorProxy enums appeared. Live `T` followed the enum:

| Hold | SensorProxy enum | Live `T` (kscreen Rotation, logical size) |
| --- | --- | --- |
| Landscape A (rest) | `left-up` | `Rotated90` (`2`, `1280×800`) |
| Portrait A | `normal` | none (`1`, `800×1280`) |
| Landscape B | `right-up` | `Rotated270` (`8`, `1280×800`) |
| Portrait B | `bottom-up` | `Rotated180` (`4`, `800×1280`) |

That is the standard proxy → KWin map. Portraits and both landscapes already match. The cheap “portraits fine, landscapes swapped” diagnostic is **not** present on this box now — the wiki matrix already corrected the frame. T4 four-hold *solve* is still later; this pass is observation only.

## T2 contract (from this probe)

- Match `platform:cros-ec-accel` / `name=cros-ec-accel` (not every `iio*` node).
- Persist `0, 1, 0; -1, 0, 0; 0, 0, -1`.
- Apply is udev + `iio-sensor-proxy` reload (poll path).
- Do not compose on sysfs (empty `in_accel_mount_matrix`).
- Do not use `LIBINPUT_CALIBRATION_MATRIX` for gravity.
- Follow still does not write `T`.

The T1 Tilt card should show **readable**, identity `cros-ec-accel` / `platform:cros-ec-accel`, empty kernel matrix, wiki `udev ACCEL_MOUNT_MATRIX`, and live `enum=` next to live `T=` (not the persisted json transform).

T2 apply: `61-tiltback-accel.rules` is one line matching `name=cros-ec-accel` / `platform:cros-ec-accel` (not `KERNEL=="iio*"`). Identity (`1, 0, 0; 0, 1, 0; 0, 0, 1`) is the test that changes the landscape enum; Revert deletes our file and the wiki leak restores the previous enum. Leave `/etc/udev/rules.d/61-cros-ec-accel.rules` in place.

T2 live apply/revert (2026-09-22). One polkit auth installed `apply-accel.sh` and enabled `tiltback-accel.path`. After that the session only writes `/run/tiltback/accel-request`.

Identity wrote exactly:

```text
SUBSYSTEM=="iio", ACTION!="remove", ATTR{name}=="cros-ec-accel", ATTRS{modalias}=="platform:cros-ec-accel", ENV{ACCEL_MOUNT_MATRIX}="1, 0, 0; 0, 1, 0; 0, 0, 1"
```

Live `udevadm info` on `iio:device2` flipped wiki → identity → wiki. `udevadm test` on `iio:device0` and `event0` kept the wiki leak value (not identity). Revert deleted our file. Wiki hash stayed `d7260870ffff7d9540c49b78aa13b73e66cf245906dad1485fb482e7005f72f0`. `name=iio:device2` is rejected in dest-test.

Enum stayed `undefined`: raw was Z-dominant (`~0, ~-120, ~16700`) the whole time. That is the flat rest the plan warned about — a landscape hold is required to see `left-up` ↔ `right-up`. After proxy restart the sticky pre-apply `left-up` is gone until the next non-flat reading.

T3 cycle (2026-09-22, `user@10.0.0.117`). Path unit still enabled. Landscape-ish hold, enum `left-up`, live `T` `Rotation 2` (`Rotated90`). Identity (`0/8`) changed live udev to identity and the enum to `bottom-up` (no reboot). Wiki (`5/8`) restored udev and enum `left-up`. Our rule stayed one well-formed line on `cros-ec-accel` only; `udevadm test` on `iio:device0` / `event0` still showed the wiki leak value, not a second identity. Wiki file hash unchanged. Keep left `61-tiltback-accel.rules` at the wiki 3×3. `accelMountMatrix` is the `home.json` field Save home writes; follow does not apply it. T0 already showed both portraits match this wiki frame.

## What this proves about Tilt

1. **T2 chassis is this tablet**, not the W620 (no IIO) and not the CachyOS RT08WT (IIO unreadable).
2. Honesty state is **readable + wiki-corrected**. T2’s first apply here is “install our helper with this identity and this matrix,” not a hunt.
3. Preview is live `AccelerometerOrientation` next to live `T`. Do not trust persisted `kwinoutputconfig.json` `transform` as the tip clock.
4. A broken wiki wrap leaked `ACCEL_MOUNT_MATRIX` onto non-accel nodes. The helper must not repeat that.
