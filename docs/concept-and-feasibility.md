# TiltBack: concept and feasibility

A Linux-native GUI for diagnosing and correcting tablet orientation mismatches between the display, stylus, touch, and pointer cursor.

This note started as a concept discussion. A Plasma Wayland clinic now exists; the analysis below is still the model. What shipped, what was dropped, and the next fork in the road are at the end.

## Why this exists

On Windows tablets, display rotation, digitizer mapping, touch mapping, and the pointer sprite are usually treated as one coordinated transform. The firmware reports a panel orientation; the HID stack and DWM consume it together. When the report is wrong, Windows still tends to keep the four layers in sync with each other.

On Linux the same hardware is split across four loosely coupled subsystems. Each one can be correct in isolation and still produce a machine that is unusable as a tablet:

1. The desktop comes up in portrait even though the panel is physically landscape (or the reverse).
2. While the picture is portrait, the stylus already maps as landscape, so the pen draws beside the contact point.
3. Finger touch is a third evdev device and can have a third default mapping.
4. The usual fixes are compositor-specific command lines. They are unreliable on Wayland, and a tablet with no keyboard cannot comfortably run them.
5. Some desktops already have a piece of this. KDE Plasma’s Drawing Tablet panel is the best stylus UI on Linux today, but it is KDE-only and does not own generic touchscreens. GNOME’s Wacom panel is the same idea in the other direction. Neither is a general “make this tablet make sense” tool.
6. The most disorienting failure is an inverted mouse cursor: the hotspot may track, but the sprite points down and right instead of up and left. The raytrektab RT08WT is a concrete example **on X11**. The same chassis on Plasma Wayland did not show it: the arrow already pointed the right way. Arrow is a compositor-cursor problem, not a given on every RT08WT session.

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

The spatial wizard was built and then removed. On a sideways session the corner/edge chrome was harder to aim than the dashboard’s Left / Right and R=0 / R=8 buttons — those at least sit under a cursor. The clinic UX is the four cards, independent Keep/Revert, Save home, and follow. A future keyboard-free path can return if it is a thin skin over that same apply path, not a second control language.

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

The clinic should apply **session** immediately (the tablet has to become usable *now*) and offer **user** plus **system** as a second step. Generating a kernel cmdline or a quirk-table snippet is a power-user export, not the default path.

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

The RT08WT symptom is this class **on X11**: landscape-class 800×1280 DSI panel, Intel cursor plane, a 180° or “identity vs panel-orientation” disagreement. On Plasma Wayland the same machine did not need a software-cursor workaround. Treat the RT08WT as a **design partner** (Picture + residual + DSI builtin), and treat Arrow as an X11 / compositor-cursor test, not as a special-case fork of the dashboard.

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

**Qt 6 Quick (QML) + a C++ core** is the fit. Not GTK4, not PySide6.

- Custom painted “orientation diagram” and overlay are natural in QML/Qt Quick.
- Qt’s pointer device API distinguishes mouse, touch, and stylus without fighting GDK’s event condensation.
- The app must feel native on Plasma *and* GNOME *and* Sway. A Qt Quick utility on GNOME is normal. A libadwaita utility on Plasma is a GNOME app wearing a costume, and libadwaita fights custom chrome.
- KDE users who already trust the Drawing Tablet KCM will not be asked to learn a second visual language for the same problem space.
- Shipping one fullscreen `ApplicationWindow` for calibration is a solved Qt pattern.
- These chassis (W620 m3-7Y30, RT08WT Gemini Lake) choke on a Python session helper. The first follow daemon woke `kscreen-doctor` and a pile of `busctl` processes every 0.4s, three copies at once. The clinic and its follow helper ship as one C++ process speaking D-Bus in-process (`QDBus`). `tools/tiltback-w620` is a historical chassis recipe; Phase 4’s C++ `--follow` is the lock.

GTK4 remains a reasonable alternative if the project later wants a GNOME Circle aesthetic, or for a tiny Sway-only helper. It is the wrong default for a DE-agnostic clinic.

Language split:

- **QML** for every screen a thumb touches.
- **C++** for udev, evdev, D-Bus, DRM probe, and backends. CMake + Qt 6 (Quick, DBus, Gui).
- No PySide6 MVP. Python in this repo is probe/recipe only.

## Architecture (when it is time to build)

```
                    ┌─────────────────────────┐
                    │  QML dashboard (clinic) │
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

## Suggested shape of an MVP

The smallest thing that would have saved these chassis an afternoon. Status against that list:

1. **Done.** Diagnostic dashboard for the four layers.
2. **Dropped.** Spatial wizard. The dashboard apply buttons are the clinic.
3. **Done.** Session apply + revert countdown (Picture *and* residual).
4. **Done (KWin).** Home tuple in `home.json` + `kcminputrc` `Orientation=`.
5. **Done (KWin).** Follow-output C++ helper (`tiltback --follow`).
6. **Half.** KWin is the only backend. Xorg is still a next-step option, not a given.
7. **Not needed on the Wayland sessions we have.** Arrow is still a card that says “not inverted / not probed.” The RT08WT inverted sprite is an X11 leftover.
8. **Not started.** Copy-report exists; udev / `video=` / quirk export does not.

Out of MVP remains: kernel cmdline installer, community profile service, accelerometer integration, fake cursors, GTK port, udev-as-default residual.

## Verdict

The software is **feasible as a userspace clinic**, not as a second compositor and not as a universal hardware-cursor fixer.

The hard parts are not drawing a Qt window. They are (a) modeling four independent transforms without double-applying them, (b) speaking several compositor dialects, and (c) remaining usable when the input device the user must tap with is the device that is wrong.

Those are tractable. The alternative — memorizing a different command for every desktop, on a machine that may not have a keyboard, while the cursor points at the floor — is what people actually do today.

If TiltBack does one thing well, it should be this: **show the disagreement, let a thumb resolve it, and remember the answer for that chassis.**

## Entry point

Do not start by inventing a new protocol or another kernel quirk. Start with a machine that already has the “fundamental” fix and is still wrong.

Case 1 is a Samsung Galaxy Book 10.6 (SM-W620) on postmarketOS Plasma 6.6 Wayland: native 1280×1920 panel, live DRM `panel orientation=RIGHT_UP` from the 2021 kernel quirk, KWin persisted `Rotated270`, and identity residuals on both the Synaptics touchscreen and the Wacom I2C stylus. The picture is upside down; finger and pen are not. See [case-galaxy-book-w620.md](case-galaxy-book-w620.md).

That case is the first backend (KWin/KScreen), the first two-step clinic (picture, then residual), and the first proof that a userspace clinic is still needed after the kernel has done its job. The C++/QML clinic and the follow helper are what that case asked for; they are in the tree.

## Current state (2026-09-21)

TiltBack is a **Plasma 6 Wayland clinic**, not yet a DE-agnostic tool. One C++/QML binary (`tiltback`) does the GUI, `--follow`, `--install-follow`, `--save-home`, and `--report`.

### What a user can do

- Open a maximized dashboard: Picture, Finger, Pen, Arrow, Home / Follow.
- Apply Picture **Left** / **Right** (KScreen via one-shot `kscreen-doctor` after `org.kde.KScreen` `/backend` `getConfig`). Builtin outputs are `eDP*`, `DSI*`, `LVDS*`. `kscreen-doctor` printing “not found” is a failure even if it exits 0.
- Apply Finger / Pen **R=0** / **R=8** via KWin `InputDevice` `orientationDBus` (`Int32` only, no `calibrationMatrix`). Identity is name + VID:PID, never `eventN`. Denied: touchpads, Samsung cover `04e8:a00a`, Wacom `WCOM0028` Mouse.
- Independent 10-second Keep / Revert per layer. Follow is held off for ~15s (`~/.cache/tiltback/clinic-hold` or `/tmp`) so a clinic write is not restamped away.
- **Save home** writes `home = (T, R_touch, R_pen)` to `~/.config/tiltback/home.json` (or `/tmp` if home is RO) and decimal `vendor/product` groups in `kcminputrc`.
- **Install/start** drops a systemd `--user` `tiltback-follow.service`. On RO home it uses a runtime drop-in under `/run/user/…` and masks the old Python `tiltback-w620` unit.
- Follow restamps **R only**. It watches KWin `PropertiesChanged`, `~/.config` / `~/.local/share/kscreen`, the home.json directory, and a 0.4s tick (KWin often zeros `R` without a notify). Reloading `home.json` restamps the new R. The GUI process must not also construct `FollowEngine`.
- Copy report for a bug dump. Packaged icon is `org.tiltback.TiltBack` in hicolor plus a Qt resource; do not set QML `ApplicationWindow.icon` — that property does not exist on this Controls build and the window fails to load.

### Proven chassis

| Machine | Panel | DRM hint | Home that worked | Notes |
| --- | --- | --- | --- | --- |
| Galaxy Book W620 (pmOS Plasma 6.6 Wayland) | `eDP-1` 1280×1920 | `RIGHT_UP` (3) | `T=Rotated90`, `R_touch=8`, `R_pen=8` | Device-space 180° at every pose. KWin zeros `R` on every `T` change; follow restamps 8. Cover / mouse stay at 0. |
| raytrektab RT08WT (pmOS Plasma Wayland) | `DSI-1` 800×1280 | `BOTTOM_UP` (1) | `T=Rotated180`, `R_touch=8`, `R_pen=8` | Wayland arrow **not** inverted. Goodix `0416:038f` is the real touch; Wacom `2D1F:011E` Stylus is the pen. A non-Stylus Wacom node at `R=0` can steal first-touch pick. |

See [case-galaxy-book-w620.md](case-galaxy-book-w620.md) and [home-offset-and-follow.md](home-offset-and-follow.md).

### How it is built and installed

Two paths; do not mix their binaries. See [build.md](build.md).

| Path | Command |
| --- | --- |
| Host (Debian, Fedora, Arch, openSUSE, native Alpine) | `./scripts/deps.sh` then `./scripts/build.sh --install` → `~/.local` |
| postmarketOS musl from a glibc PC | `./scripts/build-alpine.sh` (Alpine 3.22 Docker) |

QML is interpreted (`NO_CACHEGEN`). The container’s Qt is 6.8; pmOS 26.06 is 6.11.

On the tablet: `~/.local/bin/tiltback`, desktop file with a full `Exec=` path when `~/.local/bin` is not on `PATH`, hicolor icons under `~/.local/share/icons`. Launch the GUI via the Plasma launcher or `systemd-run --user` so it inherits `WAYLAND_DISPLAY` / session bus. If home is emergency-RO, stage the binary in `/tmp`. Never `pkill -f tiltback` (it matches SSH).

### What is not there

- No extracted backend interface. `ClinicModel` talks to KWin/KScreen directly.
- No GNOME, Phosh, wlroots, or X11 apply path.
- No software-cursor switch. Arrow is diagnose-only.
- No udev / hwdb / `video=` export (Phase 7).
- No wizard. Phase 5 was implemented, then removed as more confusing than inverted dashboard controls.

`tools/tiltback-w620` and the Python follow recipe remain in the tree as history. Do not run them beside C++ follow.

## Build order (as executed)

Ship **Qt 6 Quick + C++** (CMake). No PySide6.

**Phase 0 — App skeleton. Shipped.** CMake project, C++ `QGuiApplication` + QML `ApplicationWindow`, `.desktop` file on Plasma Wayland.

Build on a fast machine in Alpine 3.22 musl (the W620 is too slow, and a host glibc binary will not run on postmarketOS). The container’s Qt is 6.8; pmOS 26.06 is 6.11 — QML is not precompiled (`NO_CACHEGEN`) so the tablet’s engine can load it.

```sh
./scripts/build-alpine.sh
scp build-alpine/tiltback data/org.tiltback.TiltBack.desktop user@W620:
```

On the tablet: copy the binary to `~/.local/bin/tiltback` and the desktop file to `~/.local/share/applications/`. Launch `tiltback` on `wayland-0`, or from the Plasma app launcher.

**Phase 1 — Probe and four-card dashboard (read-only). Shipped.** Four cards — Picture, Finger, Pen, Arrow — filled from DMI, DRM `panel-orientation`, KScreen `T`, and KWin `InputDevice` residuals via `QDBus` (never fork `kscreen-doctor` in a loop). Identify devices by udev name / VID:PID, never `eventN`. Deny the type-cover class. Arrow may say “not inverted / not probed.” A “copy report” dump is the bug-report seed.

Done when the W620 dashboard shows `T=Rotated90` and `R=8` on both digitizers without writing anything.

**Phase 2 — Session apply for Picture, with revert. Shipped.** One backend method: set the KWin/KScreen output transform. 10-second “keep this?” that restores the previous `T`. Do not touch residuals yet.

Done when you can flip the W620 `left` ↔ `right` and the countdown puts the picture back.

**Phase 3 — Session apply for Finger and Pen, same revert. Shipped.** Write `orientationDBus` on the *named* absolute devices only. Revert restores the previous `R` on each layer independently. Applying Finger must not silently write Pen.

Done when the W620 can go `R=0` → `R=8` and back, on touch and stylus separately, without the cover or relative mice moving.

**Phase 4 — Home tuple, persist, follow. Shipped.** The model the W620 unlocked, in-process in the C++ app, not a Python poller.

- Store `home = (T_home, R_touch, R_pen)`
- Persist in compositor user config (`kwinoutputconfig.json` + `kcminputrc`)
- Follow: KWin `PropertiesChanged`, directory watch on `kwinoutputconfig.json` (inode replace), and a cheap in-process Get of the two digitizer orientations. Never write `T` unless the user asked. Never spawn `kscreen-doctor` or `busctl` in a loop. One process.
- W620 rule: `R(T) = 8` for all four poses, because KWin zeros `R` on every transform change.
- `tools/tiltback-w620` becomes a profile of this helper, then goes away.

Done when Display Configuration’s 15s revert (or a manual pose change) leaves finger and pen on the picture **immediately**, not on a 30s safety timer.

**Phase 5 — Wizard (keyboard-free). Tried, then removed.** The spatial UI was more confusing than changing a setting with inverted controls. The dashboard *is* the keyboard-light clinic: large buttons, Keep/Revert, Save home. Do not rebuild the wizard unless a later pass is strictly a skin over the same apply/hold path.

**Phase 6 — Arrow, then the RT08WT. RT08WT dashboard done; Arrow apply not started.** The same clinic pointed at the RT08WT: DSI builtin, home + follow, no kernel quest. Wayland did not need a software-cursor workaround. Arrow remains a yes/no plus a backend switch **when a session actually inverts the sprite** (X11 on this chassis, or another compositor).

**Phase 7 — Export only. Not started.** Draft udev / `video=` / quirk snippet as text the user can copy. Polkit udev apply and kernel-cmdline install stay out.

**Phase 8 — Second backend. Not started.** Same narrow interface: list outputs, get/set `T`, list absolute devices, get/set `R`, “can you force a software cursor?” The original note put Xorg here, then Mutter/wlroots/Hyprland. That fork is the next-steps question below.

**Skip or defer:** udev as the default residual, IMU/auto-rotate, fake cursors, GTK, PySide6, button/pressure editors, the cover touchpad, community profile service, a second wizard.

**Why that order still holds:** Phase 1 is useful alone and is how the RT08WT was met. Phases 2–3 force apply/revert before anything can double-transform anyone. Phase 4 is the Plasma MVP because without follow the clinic dies in Display Configuration. Phase 5 as a separate UX language did not pay off. Phase 6’s unproven layer (hardware cursor) did not appear on the Wayland sessions we have. C++ from Phase 0 so the W620-class CPU never runs a Python session helper again.

## Next steps

The Plasma clinic works on two pmOS tablets. The next fork is not “more W620 probes.” It is one of:

1. **A build script and document for other Linux distros, still KDE Plasma.**
2. **A (partially) new backend for GNOME and Phosh on Wayland.**
3. **A new backend for X11.**

### Recommendation: do (1) next

The product is proven on one compositor and one libc. Nobody else can install it. A glibc build path (Fedora KDE, Debian/Ubuntu Plasma, Arch, openSUSE) plus an install note is the cheapest way to get a third chassis and to find out whether KWin 6.3–6.6 still speak the same `orientationDBus` / KScreen `/backend` dialect.

What (1) should contain, and nothing more:

- Native `cmake` on the distro’s Qt 6 (Quick, QuickControls2, DBus) + libdrm. Keep `NO_CACHEGEN` unless the build Qt and the session Qt are the same V4 cache version.
- `cmake --install` prefix: binary, `org.tiltback.TiltBack.desktop`, hicolor icons.
- How to enable `tiltback-follow.service` on a writable home (the RO `/tmp` + runtime drop-in path is a pmOS emergency, not the default).
- “Plasma Wayland only. This binary will not rotate GNOME or X11.”
- A one-page matrix: Alpine musl container vs host glibc.

That is packaging and honesty, not a new orientation model. It also forces a tiny bit of hygiene (`CMAKE_INSTALL_PREFIX`, desktop `Exec=tiltback` on PATH) that the SSH-to-`~/.local/bin` workflow never needed.

Do **not** wait for (1) to fix small Plasma bugs that already bit testers: first-touch pick when a chassis exposes both Goodix and a Wacom non-Stylus node; Picture **none** / **inverted** buttons if Left/Right is a poor fit for a given home. Those are still KWin work and stay in this backend.

### Then extract a backend interface before (2) or (3)

`ClinicModel` is the KWin backend. A second dialect copied into the same file will double-transform someone. The feasibility note already named the narrow interface: list outputs, get/set `T`, list absolute devices, get/set `R`, “can you force a software cursor?” Follow is part of that interface (subscribe to pose change, restamp `R`, never write `T`). Extract it once, keep the QML dashboard, then add a backend.

### (3) X11 before (2) GNOME/Phosh — if a second *apply* backend is the goal

X11 is the smaller second dialect and the only place our hardware has shown Arrow.

| | X11 | GNOME + Phosh Wayland |
| --- | --- | --- |
| Display | RandR / `xrandr` | Mutter DisplayConfig / `gdctl` **or** phoc `wlr-output-management` — **not the same API** |
| Input residual | XInput CTM (`xinput`) | GSettings Wacom + udev residual; Phosh/phoc is compositor IPC + udev |
| Follow | RandR notify + rewrite CTM | Mutter `MonitorsChanged` **or** a wlroots listener |
| Software cursor | `SWCursor` — actually exists | Mutter often already drops to SW cursor; Phosh unknown |
| Double-transform trap | Mild (CTM is the lever) | The main design risk in this document |
| Fits our machines | RT08WT inverted arrow is here | New testers, new persist format |

Calling “GNOME and Phosh” one backend is a mistake. Phosh’s compositor is **phoc** (wlroots family). GNOME’s is **Mutter**. They share a shell aesthetic, not a D-Bus. Option 2 is two backends wearing one label.

X11 is strategically the past. It is still the right *second* backend if the point is to prove the interface and to finish Arrow on a machine we already own. It is the wrong second backend if the point is “pmOS users who picked Phosh.” Those are different products for a month of work.

### (2) GNOME / Phosh — later, and split

Worth doing when (1) has put the Plasma clinic on a glibc box and the backend interface exists. Then:

- **Mutter first** if the tester is a GNOME tablet (DisplayConfig + GSettings + honest “touch is already mapped to the output”).
- **phoc/Phosh second** if the tester is a pmOS phone/tablet without Plasma (`wlr-output-management`, same udev residual rules as other wlroots).

Do not invent a udev matrix as the default residual on Mutter “because we already have udev on the list.” Prefer the compositor API; udev is the leftover, and it double-rotates.

Follow on GNOME is a new engine, not a `#ifdef` in `FollowEngine`. Persist is not `kcminputrc`.

### What not to pick next

- Rebuilding the wizard.
- A software-cursor overlay or a rotated cursor theme (still rejected).
- Kernel cmdline install / Polkit udev apply (Phase 7 is text export only).
- Hyprland/Sway before Mutter or X11 — we have no chassis there, and no install story even for Plasma.

### Practical order

```text
1. Other-distro Plasma build + install note     ← do this
2. Small KWin leftovers (digitizer pick, …)     ← in parallel if they bite
3. Extract Backend { T, R, follow, cursor? }    ← required before a second DE
4. X11 backend  — if Arrow / RT08WT X11 matters
   or Mutter    — if a GNOME tablet is the next machine
5. phoc/Phosh   — after Mutter or after a Phosh tester appears
6. Phase 7 text export                          ← anytime; does not block 1
```

If TiltBack does the next thing well, it should be this: **the same Plasma clinic, installable on a normal KDE laptop, with an honest “this is KWin” line — then a second backend that is one compositor, named correctly.**