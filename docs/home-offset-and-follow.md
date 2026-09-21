# Home offset and follow-output

The W620 session fix works in one pose. It is not a complete product until the same alignment survives Plasma’s other display orientations. This note is the design that case unlocked.

## What we actually stored

Two independent KWin knobs:

| Knob | Where | Value that fixed the W620 |
| --- | --- | --- |
| Output transform | `kwinoutputconfig.json` / KScreen | `Rotated90` (`left`) |
| Device orientation | `kcminputrc` `Orientation=` | `8` (`Qt::InvertedLandscapeOrientation`) on touch and stylus |

Plasma System Settings → Display Configuration writes the first. The Drawing Tablet panel and KWin’s `InputDevice` API write the second. Changing one does not recompute the other. That is why a correct pair at `left` does not stay correct when the user picks `none`, `right`, or `inverted`.

`Orientation=8` is not “stay 180° relative to the picture.” It is an **absolute extra matrix** in device space, applied by libinput, with a name that is already landscape-specific. KWin may still map the device through the current output transform (it did when we flipped `right` → `left`), but it will not rewrite `Orientation` to match a new pose.

## Why a static residual breaks at other poses

Write the mapping as:

```text
picture(T)     = T
input(T, R)    = M(T, R)
```

`T` is the output transform the user just chose in Plasma. `R` is the KWin device orientation / calibration we persisted.

On the W620, measured facts:

1. `T = Rotated270`, `R = 0` → inputs match the chassis, picture is 180° off.
2. `T = Rotated90`, `R = 0` → picture matches the chassis, inputs are 180° off. The **gap followed the output**.
3. `T = Rotated90`, `R = InvertedLandscape` → picture and inputs both match.

So `R` is the value that cancels the gap **at that T**. It is a **pose-specific residual**, not a home definition.

Even in the tidy model `input = T ∘ R_device`, a device-space 180° that is correct in landscape is an upside-down finger on an upright *portrait* desktop (`T = Normal`). `InvertedLandscape` is the wrong extra transform the moment the output is no longer a landscape. Tablet-tool paths are worse: KDE’s tablet orientation is historically an absolute dropdown, not “output transform plus offset.”

The clinic can stop at step 3. A usable tablet cannot.

## The missing concept: home versus pose

Split what we measured into two objects.

**Home** is the chassis. For this hold of the W620 (type cover at the bottom):

```text
home.output        = Rotated90
home.input[touch]  = InvertedLandscape
home.input[pen]    = InvertedLandscape
```

That pair is the device profile. It is what the wizard saves.

**Pose** is whatever the user (or a future accelerometer) wants *now*: `none` / `left` / `right` / `inverted`, later the eight flip variants.

TiltBack’s job after the wizard is:

```text
T      ← KScreen current output transform
R(T)   ← residual that makes input(T, R) agree with picture(T)
         given the saved home pair
apply R(T) to each absolute device
leave T to Plasma
```

Plasma keeps being the rotation UI. TiltBack keeps being the gap UI. They must not overwrite each other’s idea of `T`.

## How to compute R(T)

Three implementations, cheapest first.

### 1. Constant device-space residual (this is the W620)

If picture and inputs disagree by a **device-space** 180° at every pose, `R` does not depend on `T`:

```text
R(T) = InvertedLandscape   # for all four Plasma rotations
```

Portrait (`none` / `inverted`) is the test that distinguishes this from a landscape-only gap. On the W620 they are the same 180° upside-down as the original bug when `R = 0`. Follow still matters because KWin drops `R` to `0` whenever `T` changes.

Conjugation `T⁻¹ ∘ G ∘ T` is for a *screen-space* gap. A 180° rotation commutes with 90° steps, so that formula also yields a constant 180° — same answer, different story. Measure portrait before assuming either story.

### 2. Four-row table (always correct, a bit ugly)

The wizard, or a one-time derivation from the home pair, fills:

| Plasma rotation | T | R(touch) | R(pen) |
| --- | --- | --- | --- |
| left | Rotated90 | InvertedLandscape | InvertedLandscape |
| right | Rotated270 | ? | ? |
| none | Normal | ? | ? |
| inverted | Rotated180 | ? | ? |

Unknown cells can be computed from (1) and then confirmed with the same tap-the-corner test. Persist the table in the device profile. A session helper only does lookup + apply.

This is the first profile format that is honest about tablet PCs: **not one matrix, a function of output transform.**

### 3. Redefine Normal so Plasma rotates around home

Longer-term, make `T = Rotated90` the stack’s idea of “none”:

- export `video=eDP-1:panel_orientation=left_side_up` (the opposite of the upstream `RIGHT_UP` quirk), or
- teach the compositor a home offset that Display Configuration treats as identity.

That does **not** remove follow. On the W620, switching the kernel hint alone reproduces the state after step 1 of the experiment (picture upright, inputs inverted). Inputs still need a residual. The value of redefining Normal is UX: Plasma’s four rotation buttons then mean what the user thinks, and auto-rotate (if an IMU ever appears) is not off by 180° in landscape. Follow still applies `R(T)`.

## The session helper

A correct `kcminputrc` is inert when the user later clicks Display Configuration. TiltBack needs a **user-session helper** (autostart, no root):

1. Subscribe to KScreen / KWin output config changes.
2. Ignore changes that are not a transform (brightness, scale).
3. Recompute `R(T)` from the profile.
4. Set `orientationDBus` / `calibrationMatrix` on each profiled device.
5. Do not write `T` unless the user asked TiltBack to change the picture.

If the helper is not running, the W620 is fixed only until the next Plasma rotation. That is an acceptable first-boot, not a product.

This is also where “lock layers together” becomes real. After home is measured, follow is the lock: picture moves, finger and pen are rewritten so they stay on the picture. Unlocking a layer means “stop updating that device when `T` changes,” which is how a user would debug a single broken sensor.

## What this adds to TiltBack

The W620 did not just prove the Plasma backend. It promoted several concept-note ideas from polish to the core model.

| Feature | Before the W620 | After |
| --- | --- | --- |
| Four-layer dashboard | Planned | Proven useful; would have shown `T=270, R=0` vs `T=90, R=8` |
| Rotate picture only | Hoped it was enough | Disproved; inputs followed `T` |
| Per-device residual | Theoretical trap | Required; same 180° on touch and pen *for this pose* |
| Home vs current pose | One formula for auto-rotate | **The** data model. Home is a `(T, R_touch, R_pen)` tuple |
| Follow-output | Mentioned as not fighting iio-sensor-proxy | **MVP.** Without it the clinic fix dies in Display Configuration |
| Device profile | DMI + matrices | DMI + home tuple + `R(T)` table |
| Session helper | Not listed | Autostart watcher, KWin/KScreen backend |
| Lock layers | UI toggle | Follow is the implementation |
| Apply / revert | Display countdown | Must cover residual apply too (we flipped inputs twice) |
| Kernel quirk export | “more correct” | Optional. Upstream `RIGHT_UP` is live and still wrong for this hold. A `LEFT_UP` snippet is a profile extra, not a substitute for follow |

Out of scope remains the same: TiltBack is not a compositor and not a replacement for Plasma’s rotation picker. It is the piece Plasma is missing — **remember the chassis, and keep the sensors on the picture when the picture moves.**

## Proof on the W620 (2026-09-21)

The live recipe is C++ `tiltback --follow` (one systemd `--user` `tiltback-follow.service`). Do not also Plasma-autostart it.

The first helper was Python: it polled `kscreen-doctor -j` every 0.4s and walked every KWin input with `busctl` every 2s. Three copies (unit + two `.desktop` files) produced the ~80% CPU spike. Follow now does that work in-process: D-Bus Gets on the two digitizers, a directory watch (KWin replaces the `kwinoutputconfig.json` inode), and a 0.4s tick because KWin often zeros `R` without a notify.

First rule (wrong): landscape `R=8`, portrait `R=0`. Tap test: 90° and 270° correct; **none and 180° still 180° upside down**.

That first “Plasma none breaks inputs” report was not evidence that `R=8` is wrong in portrait. KWin **zeros `R` on every `T` change**, so the none preview was `none + R=0` — the same upside-down pair we then installed on purpose.

Corrected rule for this chassis:

```text
R(T) = InvertedLandscape (8)  for none, left, inverted, and right
```

The 180° gap is in **device space** (native portrait sensors). It is present at every pose. Follow is still required: not to pick a per-`T` residual, but to **re-stamp `R=8` after KWin wipes it**.

| T | First helper | Tap result | Correct `R` |
| --- | --- | --- | --- |
| left / 90° | 8 | match | 8 |
| right / 270° | 8 | match | 8 |
| none / 0° | 0 | upside down | 8 |
| inverted / 180° | 0 | upside down | 8 |
