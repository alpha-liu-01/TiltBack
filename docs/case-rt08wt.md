# Case: raytrektab RT08WT (CachyOS)

T5 chassis. Probed over SSH on 2026-09-22 from `alpha@10.0.0.109`. Picture / residual home was already saved (`T=Rotated180`, `R_touch=8`, `R_pen=8`). This note is the IMU layer: poll-readable `KIOX000A`, dead ring buffer, identity kernel matrix.

Do not treat the buffer EPERM as “no sensor.” Do not chmod `/dev/iio:device0`. Follow does not write `T`.

## Machine

| Field | Value |
| --- | --- |
| Hostname | `alpha-raytrektab` |
| OS | CachyOS (Arch), kernel `7.2.5-1-cachyos`, x86_64 |
| Session | Plasma Wayland, uid `1000` |
| DMI vendor | `Thirdwave Corporation` |
| DMI product | `RT08WT` |
| Board | `Techvision` / `8016E` |
| Panel | `DSI-1` native `800×1280@60`, scale 1.25 |
| Auto-rotation | `Always` on `DSI-1` |
| Home | `T=Rotated180`, `R_touch=8`, `R_pen=8` (Goodix `0416:038f`, Wacom `2D1F:011E` Stylus) |

## IIO

One node. Driver `kxcjk1013`, ACPI id `KIOX000A`.

| Field | Value |
| --- | --- |
| sysfs | `/sys/bus/iio/devices/iio:device0` |
| `name` | `i2c-KIOX000A:00` |
| raw | world-readable (`644`); scale `0.019163` |
| kernel `in_accel_mount_matrix` | identity `1, 0, 0; 0, 1, 0; 0, 0, 1` |
| `/dev/iio:device0` | `root:root` `0600` — unused by poll |
| trigger | none (`trigger/current_trigger` empty; no `iio_sysfs_trigger`) |
| buffer | `buffer/enable` EPERM every boot (`iio-sensor-proxy` 3.9) |
| `IIO_SENSOR_PROXY_TYPE` | `iio-poll-accel iio-buffer-accel` |
| hwdb | no `svnThirdwaveCorporation:pnRT08WT` hit |

Helper match is `ATTR{name}=="i2c-KIOX000A:00"` plus probed `ATTRS{modalias}=="acpi:KIOX000A:KIOX000A:"` (never `iio:device0`). Do not invent `platform:i2c-KIOX000A:00`. Walk udev from the canonical sysfs path; `cdUp` on `/sys/bus/iio/devices/iio:device0` stays on the bus symlink and misses the ACPI id.

```text
KIOX000A raw → udev ACCEL_MOUNT_MATRIX → iio-sensor-proxy poll → enum → KWin T
```

The buffer driver fails; poll still publishes. Copy-report: `buffer EPERM / no trigger; using poll`. Honesty stays **readable**.

## SensorProxy vs T (before apply)

Standard map, all four enums live:

| SensorProxy enum | Live `T` (kscreen Rotation) |
| --- | --- |
| `normal` | none (`1`, `800×1280`) |
| `left-up` | `Rotated90` (`2`, `1280×800`) |
| `bottom-up` | `Rotated180` (`4`, `800×1280`) |
| `right-up` | `Rotated270` (`8`, `1280×800`) |

Home `Rotated180` is the **top** bezel in the iio-sensor-proxy panel-native frame (X right, Y native top, Z out). Clinic captures are “this panel bezel is down,” not “home is bottom.”

## T5 apply

Package `tiltback 0.0.0-4` ships `apply-accel.sh` and `tiltback-accel.path`. Discrete set is ten (T3 eight plus 180° about X / Y). Solve scores sysfs raw; apply is the T2 helper. No poll-force line unless a post-apply edge-hold stays `undefined`.

Labeled four-hold (2026-09-22, `tiltback 0.0.0-4`). Panel bezels, not “home is bottom.” First right capture was +Y (collinear with top) and the solver refused (residual 0.54). Recaptured right as +X.

| Bezel down | sysfs raw | enum at capture | live `T` |
| --- | --- | --- | --- |
| bottom | `55,-494,-172` (−Y) | `normal` | `Rotated90` (KWin lag; map is `normal`→none) |
| right | `392,38,-335` (+X) | `left-up` | `Rotated90` |
| top | `2,481,-301` (+Y) | `bottom-up` | `Rotated180` (home) |
| left | `-477,134,-223` (−X) | `right-up` | `Rotated270` |

Solve snaps to identity `0/10` (`1, 0, 0; 0, 1, 0; 0, 0, 1`). The two portraits are not swapped in the proxy panel-native frame; the earlier SSH log that called home “bottom” must not be scored. Helper apply of identity wrote one line:

```text
SUBSYSTEM=="iio", ACTION!="remove", ATTR{name}=="i2c-KIOX000A:00", ATTRS{modalias}=="acpi:KIOX000A:KIOX000A:", ENV{ACCEL_MOUNT_MATRIX}="1, 0, 0; 0, 1, 0; 0, 0, 1"
```

`udevadm` showed that `ENV` on `iio:device0` only. Revert (`op=remove`) deleted the file; no `ACCEL_MOUNT_MATRIX` remains. Enum stayed `left-up` / `T=Rotated90` across the identity reload (still on the right-bezel hold). Follow logged only `follow idle (kwin) R_touch=8 R_pen=8` — it did not write `T`. No poll-force line.

## Out of this case

`iio-trig-sysfs`, `MODE`/`GROUP` on `/dev/iio:device0`, TiltBack-as-proxy, `LIBINPUT_CALIBRATION_MATRIX` for gravity, composing udev on a non-identity kernel matrix, greeter copies of the accel rule.
