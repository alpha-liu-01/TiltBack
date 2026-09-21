# TiltBack: concept and feasibility

A Linux-native GUI for diagnosing and correcting tablet orientation mismatches between the display, stylus, touch, and pointer cursor.

This note is a concept discussion, not a specification. No implementation decisions beyond toolkit direction are locked.

## Why this exists

On Windows tablets, display rotation, digitizer mapping, touch mapping, and the pointer sprite are usually treated as one coordinated transform. The firmware reports a panel orientation; the HID stack and DWM consume it together. When the report is wrong, Windows still tends to keep the four layers in sync with each other.

On Linux the same hardware is split across four loosely coupled subsystems. Each one can be correct in isolation and still produce a machine that is unusable as a tablet:

1. The desktop comes up in portrait even though the panel is physically landscape (or the reverse).
2. While the picture is portrait, the stylus already maps as landscape, so the pen draws beside the contact point.
3. Finger touch is a third evdev device and can have a third default mapping.
4. The usual fixes are compositor-specific command lines. They are unreliable on Wayland, and a tablet with no keyboard cannot comfortably run them.
5. Some desktops already have a piece of this. KDE Plasma’s Drawing Tablet panel is the best stylus UI on Linux today, but it is KDE-only and does not own generic touchscreens. GNOME’s Wacom panel is the same idea in the other direction. Neither is a general “make this tablet make sense” tool.
6. The most disorienting failure is an inverted mouse cursor: the hotspot may track, but the sprite points down and right instead of up and left. The raytrektab RT08WT is a concrete example.

TiltBack’s job is to make those four layers visible, independently adjustable, testable with a finger or pen, and persistable — without requiring a physical keyboard or a particular desktop environment.

The name is literal: tilt the stack back to the orientation the hardware was built for.

## What is actually going wrong

Linux does not have a single “tablet orientation” object. It has a pipeline.

```
 firmware / ACPI / DT / DMI
            │
            ▼
 ┌──────────────────────┐     hint only
 │ DRM panel-orientation│──────────────────────────┐
 └──────────────────────┘                          │
            │                                      │
            ▼                                      ▼
 ┌──────────────────────┐                 compositor output
 │ kernel modeset / KMS │                 transform (0/90/180/270 + flips)
 └──────────────────────┘                          │
                                                   ▼
 evdev nodes (often several)              logical desktop coordinates
   • touchscreen  ID_INPUT_TOUCHSCREEN             │
   • tablet/pen   ID_INPUT_TABLET                  │
   • mouse/touchpad                                │
            │                                      │
            ▼                                      │
 libinput calibration matrix                       │
 (udev LIBINPUT_CALIBRATION_MATRIX)                │
            │                                      │
            ▼                                      │
 compositor input mapping  ◄───────────────────────┘
   (map-to-output, tablet rotation,
    left-handed, KWin/Mutter tablet state)
            │
            ▼
     Wayland / X11 client events
            │
            ▼
 hardware cursor plane  ←── often NOT the same transform as the primary plane
```

The mismatches the user sees are not one bug. They are different stages disagreeing about “which way is up.”

### 1. Wrong default screen orientation

Many x86 tablets ship a landscape LCD whose native mode is e.g. 1280×800, mounted in a landscape chassis, but the firmware never publishes a trustworthy orientation. Linux then has to guess.

Sources of the guess, in order of authority:

- Device tree `rotation` on ARM.
- ACPI / GOP on some x86 machines.
- The kernel quirk table `drm_panel_orientation_quirks.c`, matched by DMI plus panel resolution (and sometimes BIOS date). Cheap tablets often have generic DMI, so they never match.
- The compositor’s own stored display config from a previous session.
- `iio-sensor-proxy` plus the desktop’s auto-rotate daemon, which rotates *relative to whatever the compositor believes “normal” is*.

`panel-orientation` is a **hint**, not a modeset. The kernel does not rotate the desktop framebuffer for KMS clients. It tells userspace “the right edge of this panel is physically up.” If the compositor honors a wrong hint, the session starts in portrait. If it ignores a correct hint, the session starts in landscape on a portrait-mounted panel.

A kernel cmdline override exists (`video=eDP-1:panel_orientation=right_side_up` and similar). It requires a reboot and is invisible to anyone without a keyboard or serial console.

### 2. Stylus already in landscape while the picture is portrait

A built-in digitizer (Wacom ISD / “Feel IT”, Microsoft PTP, USI, AES, etc.) is almost always a **separate** input node from the panel. Its `ABS_X` / `ABS_Y` ranges follow the sensor, not the current compositor transform.

Typical failure:

- The panel path went through `panel-orientation` or a saved 90° output transform, so the desktop is portrait.
- The pen path did not. libinput still emits coordinates in the sensor’s native landscape space.
- Some compositors apply the output transform to touch but not to tablet-tool events. This has been a recurring wlroots/labwc/Sway class of bug: finger and pen subscribe to different event streams, and only one of them is pre-rotated.

So the user is not imagining a “third orientation.” The stylus really does have its own default.

KDE’s stylus settings can fix this *inside KWin*. They cannot fix it on GNOME, Sway, Hyprland, or labwc, and they do not automatically write a compositor-agnostic udev matrix.

### 3. Touch as a third default

Finger touch is yet another node (`ID_INPUT_TOUCHSCREEN`), often on a different HID collection or even a different bus (I2C HID touch vs USB/Wacom pen). It gets its own:

- axis ranges and resolution
- udev properties
- libinput calibration matrix
- compositor map-to-output rule

Desktop “tablet” panels usually bind to `ID_INPUT_TABLET` only. Generic touchscreens fall through to “touchscreen = the display,” which is true only when the display transform and the touch matrix already agree.

This is why a machine can have a correct pen, a sideways finger, and a portrait picture at the same time.

### 4. Command lines fail at the worst moment

On X11 a two-liner often works:

```text
xrandr --output eDP-1 --rotate right
xinput set-prop "ELAN Touchscreen" "Coordinate Transformation Matrix" …
```

On Wayland there is no `xrandr` and no `xinput`. Display rotation is compositor-private:

| Compositor family | Display API | Input orientation API |
| --- | --- | --- |
| Xorg | RandR (`xrandr`) | XInput CTM (`xinput`) |
| wlroots (Sway, labwc, …) | `wlr-output-management` (`wlr-randr`, `swaymsg`) | compositor IPC; optional libinput matrix |
| Hyprland | `hyprctl` / its own IPC | `hyprctl` input + libinput |
| KWin | KScreen / `kscreen-doctor` | KWin tablet D-Bus / Drawing Tablet KCM |
| Mutter / GNOME | DisplayConfig D-Bus, `gdctl` | GSettings Wacom schemas + libinput |

A snippet that works on Sway is a no-op on Plasma. A Plasma setting does not exist on GNOME. `LIBINPUT_CALIBRATION_MATRIX` is the closest thing to a universal input lever, but writing it means udev, root, and a device rebind — not something to do from memory on a tablet that cannot tap correctly.

### 5. Desktop settings cover one device class

Plasma 6’s Drawing Tablet KCM can set orientation, map a tablet to an output, and (since 6.2) run a four-point pen calibration against KWin’s matrix. That is the right UX, scoped to the wrong problem: Wacom-class pens on KDE.

GNOME’s Wacom panel and `gsetwacom` are the same scope on Mutter.

Neither is trying to be:

- a diagnostic of *why* the layers disagree
- a touchscreen orientation tool
- a cursor-sprite tool
- a Wayland-wide backend

TiltBack should not replace those panels for button mapping, pressure curves, or left-handed toggle. It should own the **orientation alignment** problem those panels leave on the table.

### 6. Upside-down mouse cursor

This one looks supernatural and has a mundane explanation.

The pointer image is not part of the desktop framebuffer. On KMS it is usually a **separate cursor plane**. The compositor must:

1. rotate/flip the primary plane (or render a rotated scene into it)
2. rotate/flip the cursor **sprite**
3. rotate the **hotspot**
4. place the plane at the transformed coordinates

If step 1 happens and steps 2–4 do not, you get a cursor that tracks (or tracks inverted) while the arrow points the wrong way. A 180° disagreement produces exactly “points down and right instead of up and left.”

Known ingredients:

- Intel i915 cursor planes historically expose 0° and 180° rotation only, not 90°/270°. Gemini Lake (the RT08WT’s UHD 600) is in that world. A compositor that “fixes” a portrait-vs-landscape panel with a 90° output transform may be unable to rotate the hardware cursor to match.
- Mutter’s long-standing workaround is to drop to a **software cursor** on a rotated CRTC, so the arrow is painted into the already-rotated scene. Other compositors do not always do this.
- Hyprland and wlroots have had repeated hardware-cursor bugs on rotated/flipped outputs. The user-facing workaround is `no_hardware_cursors`.
- On Xorg, `Option "SWCursor" "true"` is the same idea.
- A wrong `panel-orientation` of 180° can rotate the console or the primary plane while leaving the cursor plane on identity.

A normal Wayland client **cannot** program the DRM cursor plane. The compositor is DRM master. TiltBack can diagnose this and apply compositor-specific software-cursor workarounds. It cannot authoritatively rotate the hardware sprite on every stack.

A last-resort hack — shipping a pre-rotated cursor theme — fights the hotspot and every application that sets its own cursor. Treat it as a diagnostic curiosity, not a product feature.

## Prior art

| Tool | What it does well | Why it is not enough |
| --- | --- | --- |
| `xrandr` + `xinput` | Full control on X11 | Dead on native Wayland |
| `wlr-randr`, `swaymsg`, `hyprctl` | Display transform on one family | No Plasma/GNOME; input is a second tool |
| `kscreen-doctor` | Plasma display rotation | Display only |
| `gdctl` / mutter DisplayConfig | GNOME display rotation | Display only |
| Plasma Drawing Tablet KCM | Best pen UI, calibration overlay | KDE + stylus, not touch, not cursor |
| GNOME Wacom panel / `gsetwacom` | GNOME pen settings | GNOME + Wacom-class only |
| `LIBINPUT_CALIBRATION_MATRIX` via udev | Persistent, compositor-agnostic input | No GUI, needs root, easy to double-apply with output transform |
| `gptouch-python` | GNOME Wayland touch + `gdctl` | CLI/GNOME-only, not stylus/cursor |
| `xinput-calibrator` / `xlibinput_calibrator` | Four-point calibration | X11-era |
| `tabletsettings` | GTK4 GUI for Sway tablet mapping | Sway/Wacom mapping, not orientation clinic |
| kernel `drm_panel_orientation_quirks.c` | Fixes “normal” for known DMI | Slow, not user-accessible, does not touch input |
| `iio-sensor-proxy` + DE auto-rotate | Live rotation | Rotates around a possibly wrong origin |

There is no DE-agnostic, keyboard-free, four-layer orientation clinic. That is the gap.

## Product concept

TiltBack is a **tablet orientation clinic**, not another settings dump of matrices.

### The user-facing idea

A person holding a half-working tablet should be able to open TiltBack (from a menu, a `.desktop` file, or an autostart on first login) and complete a wizard with thumbs:

1. **Hold it the way you want.** Four huge edge targets: “this edge is the top.” That defines *desired home orientation*, independent of what the compositor currently thinks.
2. **Look at the picture.** If the desktop contents are sideways relative to that edge, rotate the **display** until the picture matches the chassis. Buttons live in all four corners so one of them is always in a reachable place even when the UI itself is sideways.
3. **Tap the crosshair with your finger.** If the highlight misses, cycle the **touch** transform (the eight common ones: 4 rotations × optional mirror) until tap and highlight agree. Then optionally refine with a four-corner calibration.
4. **Tap the same crosshair with the pen.** Separate transform. Do not assume it equals touch.
5. **Look at the arrow.** “Does this pointer point up and left?” If not, offer the software-cursor workaround for the current backend and explain the limit.
6. **Save.** Session now, and optionally persist (udev + compositor config) via a Polkit prompt.

A second, non-wizard page is a **diagnostic dashboard**: one row per layer, showing the current transform, the device name, how it was detected, and whether a backend can change it. This is for the user who already knows the machine, and for bug reports.

### What “independent layers” means in the UI

The UI should show four cards that can disagree:

| Layer | User language | Machine object |
| --- | --- | --- |
| Picture | The desktop image | Compositor output transform, informed by DRM `panel-orientation` |
| Finger | Where a tap lands | Touchscreen evdev + libinput matrix + compositor map |
| Pen | Where the stylus lands | Tablet evdev + libinput / KWin / Mutter tablet state |
| Arrow | How the mouse pointer is drawn | Hardware vs software cursor; compositor cursor transform |

Applying “rotate right” to Finger must not silently rotate Pen. A “lock layers together” toggle can exist for the common case where the user wants them to stay in lockstep after the first successful alignment.

### Home orientation versus current rotation

The W620 made this concrete. See [home-offset-and-follow.md](home-offset-and-follow.md).

**Home** is the chassis: the output transform plus per-device residuals that make picture, finger, and pen agree for the way the tablet is meant to be held. On that machine it is `Rotated90` + KWin `InvertedLandscape` on both absolute devices.

**Pose** is whatever Plasma (or a future accelerometer) wants now. Plasma’s Display Configuration already owns pose. It does not own the gap. A residual that is correct at home is an absolute KWin `Orientation`, so it goes stale the moment pose changes.

TiltBack therefore stores a home tuple, not a single matrix, and **follows output**: when KScreen reports a new `T`, recompute each device’s `R(T)` and apply it. Leave `T` to the desktop. Do not race iio-sensor-proxy; if an IMU exists, it should only change pose.

```text
home  = (T_home, R_touch_home, R_pen_home)
R(T)  = residual that keeps input(T, R) on picture(T)
```

Fighting the desktop’s rotation picker is how these tools become worse than `xrandr`. Following it is the feature.

### Persistence, in three heights

| Height | Survives | Privilege | Reboot? |
| --- | --- | --- | --- |
| Session | compositor IPC / X11 properties | normal user | no |
| User | kscreen, mutter monitors.xml, sway config snippet | user | no |
| System | udev rule / hwdb, kernel `video=` snippet | Polkit / root | udev: rebind; cmdline: yes |

The wizard should apply **session** immediately (the tablet has to become usable *now*) and offer **user** plus **system** as a second step. Generating a kernel cmdline or a quirk-table snippet is a power-user export, not the default path.

### What TiltBack is not

- Not a compositor.
- Not a full Wacom button / pressure / ExpressKey editor.
- Not a replacement for `iio-sensor-proxy`.
- Not a promise that every inverted hardware cursor can be fixed from userspace.
- Not an excuse to ask people to edit `xorg.conf` by hand.

## Feasibility

### Display rotation — feasible

A userspace GUI cannot modeset behind the compositor’s back. It **can** call the existing per-family APIs. This is backend work, not research.

Risk: each backend’s idea of “left” versus “right,” and of composing flips with rotation, has been wrong before (wlroots has shipped inverted transform composition). The UI should apply, show a 10-second “keep this?” countdown, and revert. That pattern is proven in display settings.

Kernel `panel-orientation` remains the *correct* fix when firmware is lying. TiltBack can detect the DRM property, show it on the dashboard, and export a cmdline. Applying it automatically to GRUB/systemd-boot is optional later and is easy to get wrong.

### Stylus and touch, independently — feasible, with one semantic trap

Setting a matrix is easy. Setting the **right** matrix is not.

On X11, the coordinate transformation matrix is applied in a well-known place. On Wayland, many compositors **already** rotate absolute devices by the output transform when the device is mapped to that output. Adding a udev `LIBINPUT_CALIBRATION_MATRIX` on top double-rotates.

So every backend needs a policy:

1. **Discover** whether the compositor maps this device to this output and whether it applies the output transform to that device class (touch vs tablet-tool are not the same answer).
2. **Prefer** the compositor’s own input API when it exists (KWin tablet, Sway `input … map_to_output` / rotation, Mutter GSettings).
3. **Use udev** only for the residual: firmware-wrong sensor axes, or compositors with no input API.
4. **Never write udev and a compositor rotation for the same residual.**

This is the main design risk in the project. The dashboard should state, in plain language, “KWin is already rotating this pen with the display” versus “this touchscreen is using a leftover udev matrix.”

Device identity must be udev properties (`ID_VENDOR_ID`, `ID_MODEL_ID`, `LIBINPUT_DEVICE_GROUP`, HID uniq), never `/dev/input/event12`.

### Keyboard-free native GUI on Wayland — feasible

This is a normal application problem. Fullscreen overlay, huge hit targets, edge buttons, and a “cycle transform” control that can be mashed with a thumb are all straightforward in Qt Quick or GTK4.

The bootstrap problem is real: if touch is rotated 180° and the window is small, the user cannot press “Rotate touch.” Mitigations, in order:

- Edge and corner controls, duplicated, so one is always under a misplaced tap after a 90/180/270 miss.
- Volume-up / volume-down as wizard “cycle / confirm” when those keys exist (common on tablets).
- A 5-second auto-cycle demo: apply the next transform, wait, revert unless confirmed.
- Autostart on first graphical login when TiltBack detects disagreeing layers, so the user is not hunting a menu with a broken pointer.

Remote SSH should remain possible but must not be the intended path.

### DE-agnostic coverage — feasible as a facade, not as one protocol

A single Wayland protocol for “set this touchscreen’s matrix” does not exist. TiltBack will be a **shell over backends**. Honest support matrix for an MVP:

| Target | Display | Touch | Stylus | Cursor workaround |
| --- | --- | --- | --- | --- |
| Xorg | yes | yes | yes | `SWCursor` snippet |
| KWin (Plasma) | yes | udev residual | KWin API + udev residual | limited; file/report + software cursor if exposed |
| Mutter (GNOME) | yes (`gdctl` / DisplayConfig) | udev residual | GSettings where applicable | Mutter often already uses SW cursor on rotate |
| wlroots (Sway/labwc) | yes | map-to-output + udev | same, watch tablet-tool path | compositor-specific |
| Hyprland | yes | hyprctl + udev | hyprctl + udev | `no_hardware_cursors` |
| Unknown compositor | diagnose only | udev only | udev only | diagnose only |

“Unknown compositor + udev” is still more than users have today, as long as the GUI is honest about what it could not rotate.

### Inverted cursor — partially feasible

| Approach | Feasible? | Notes |
| --- | --- | --- |
| Detect “sprite does not match hotspot / expected quadrant” | yes, with a yes/no wizard; automatic vision is unnecessary | |
| Force software cursor | yes where the backend has a switch | Xorg, Hyprland, some others |
| Rotate the DRM cursor plane from TiltBack | no | not a KMS client |
| Fix i915 90° hardware cursor | no, kernel/compositor | dashboard can explain |
| Pre-rotated cursor theme | technically yes, product-no | hotspot and app cursors |
| Client-side fake cursor overlay | ugly, focus/grab issues | reject for v1 |

The RT08WT symptom is likely this class: landscape 1280×800 panel, Intel cursor plane, a 180° or “identity vs panel-orientation” disagreement. TiltBack should treat that machine as a **design partner and test target**, not as a special-case fork.

### Live calibration on Wayland — feasible, two methods

A Wayland client only sees events **after** libinput and the compositor. That is enough for a four-point “tap the target” solve: each tap gives a correspondence between a known surface point and the event that arrived. The resulting matrix is a **residual in screen space** and must be converted into whatever API the backend actually writes (libinput device space, KWin matrix, …).

Opening `/dev/input/event*` directly gives cleaner math (device-space matrix) and works even when the compositor mapping is nonsense. It requires the `input` group or a small privileged helper. For a tablet clinic this is acceptable if it is opt-in and mediated by Polkit, the same way `libinput debug-events` already works.

KDE’s calibration tool talks to KWin because KWin holds the device. TiltBack should use that path on Plasma and evdev/udev elsewhere.

### Persistence and packing a profile — feasible and valuable

A unique asset this project can grow is a **device profile** (DMI product + panel mode + touch ID + pen ID → home transforms). The RT08WT would be profile zero. Exporting the same data as:

- a udev rule
- a hwdb snippet
- a kernel quirk sketch for `drm_panel_orientation_quirks.c`

…turns one user’s afternoon into the next user’s first-boot fix. That is closer to how Windows “resolves itself”: someone already mapped this chassis.

## Toolkit: Qt Quick, not GTK4

Both can draw buttons. They are not equal for *this* UI.

The interface is not a GNOME Settings page. It is a spatial tool: a live diagram of four disagreeing transforms, a fullscreen calibration overlay, and the need to tell **finger events from pen events in the same window**. Plasma’s own calibration UI is QML for that reason.

**Qt 6 Quick (QML) + a small C++ or Python core** is the better fit:

- Custom painted “orientation diagram” and overlay are natural in QML/Qt Quick.
- Qt’s pointer device API distinguishes mouse, touch, and stylus without fighting GDK’s event condensation.
- The app must feel native on Plasma *and* GNOME *and* Sway. A Qt Quick utility on GNOME is normal. A libadwaita utility on Plasma is a GNOME app wearing a costume, and libadwaita fights custom chrome.
- KDE users who already trust the Drawing Tablet KCM will not be asked to learn a second visual language for the same problem space.
- Shipping one fullscreen `ApplicationWindow` for calibration is a solved Qt pattern.

GTK4 remains a reasonable alternative if the project later wants a GNOME Circle aesthetic, or for a tiny Sway-only helper. It is the wrong default for a DE-agnostic clinic.

Language split, when code exists:

- **QML** for every screen a thumb touches.
- **C++ or PySide6** for udev, evdev, D-Bus, and process backends.
- Python is a legitimate MVP if it shortens the time to a usable wizard on the RT08WT. The backend boundary should stay clean enough to rewrite the core without throwing the QML away.

## Architecture (when it is time to build)

```
                    ┌─────────────────────────┐
                    │  QML wizard + dashboard │
                    └────────────┬────────────┘
                                 │
                    ┌────────────▼────────────┐
                    │  orientation session    │
                    │  (desired home, drafts, │
                    │   apply / revert / save)│
                    └────────────┬────────────┘
                                 │
         ┌───────────────────────┼───────────────────────┐
         ▼                       ▼                       ▼
  Probe / diagnose        Apply (session)         Persist
  • drm_info / KMS        • backend set           • udev helper (Polkit)
  • udev + libinput         display transform     • compositor user config
  • compositor query      • backend set           • optional cmdline export
  • evdev optional          per-device input
                          • cursor workaround
```

Backends implement a narrow interface: list outputs, get/set output transform, list absolute devices, get/set residual matrix or native rotation, and “can you force a software cursor?”

The diagnose step should be usable even when every apply path is “not supported,” so TiltBack still has value as a **bug-report generator** (“here is my DMI, panel-orientation, three evdev nodes, and three matrices”).

## Risks that can kill the project if ignored

1. **Double transforms.** The first support bug will be “I clicked rotate and now it is worse.” Apply/revert and a composed-matrix display are mandatory, not polish.
2. **Wayland security.** Raw evdev is powerful and easy to turn into a keylogger-shaped helper. Scope the privileged helper to calibration matrices and device rebind. Never copy evdev streams off-machine.
3. **Auto-rotate fights.** Disable or offset, do not race.
4. **Multi-output convertibles.** Map each absolute device to one output before rotating. An external HDMI monitor must not inherit the tablet’s home offset.
5. **Scope creep.** Button remapping, pressure, gesture exclusion, and palm rejection are neighboring graveyards. Orientation only.
6. **Cursor honesty.** If the backend cannot fix the sprite, say so. A lying “fixed” toggle is worse than the upside-down arrow.

## Suggested shape of an MVP (later)

Not a roadmap commitment. A feasibility boundary: the smallest thing that would have saved the RT08WT an afternoon.

1. Diagnostic dashboard for the four layers (read-only is already useful).
2. Wizard: set home edge, rotate picture, align finger, align pen.
3. Session apply + revert countdown (display *and* residual; the W620 needed both).
4. Persist the home tuple in the compositor user config (KWin: `kwinoutputconfig.json` + `kcminputrc`).
5. Follow-output session helper: on KScreen transform change, recompute `R(T)` and apply it. Without this, Display Configuration undoes the clinic.
6. Two backends: Xorg, and KWin (the W620 is the first).
7. Cursor page: detect + software-cursor workaround where the backend allows, explanation otherwise.
8. Export a bug-report bundle and a draft udev / kernel-hint snippet.

Out of MVP: kernel cmdline installer, community profile service, accelerometer integration, fake cursors, GTK port, udev-as-default residual.

## Verdict

The software is **feasible as a userspace clinic**, not as a second compositor and not as a universal hardware-cursor fixer.

The hard parts are not drawing a Qt window. They are (a) modeling four independent transforms without double-applying them, (b) speaking several compositor dialects, and (c) remaining usable when the input device the user must tap with is the device that is wrong.

Those are tractable. The alternative — memorizing a different command for every desktop, on a machine that may not have a keyboard, while the cursor points at the floor — is what people actually do today.

If TiltBack does one thing well, it should be this: **show the disagreement, let a thumb resolve it, and remember the answer for that chassis.**

## Entry point

Do not start by inventing a new protocol or another kernel quirk. Start with a machine that already has the “fundamental” fix and is still wrong.

Case 1 is a Samsung Galaxy Book 10.6 (SM-W620) on postmarketOS Plasma 6.6 Wayland: native 1280×1920 panel, live DRM `panel orientation=RIGHT_UP` from the 2021 kernel quirk, KWin persisted `Rotated270`, and identity residuals on both the Synaptics touchscreen and the Wacom I2C stylus. The picture is upside down; finger and pen are not. See [case-galaxy-book-w620.md](case-galaxy-book-w620.md).

That case is the first backend (KWin/KScreen), the first two-step clinic (picture, then residual), and the first proof that a userspace clinic is still needed after the kernel has done its job. The session pair is confirmed; follow-output is the remaining half.
