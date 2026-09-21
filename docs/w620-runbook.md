# W620 wrap-up and how to re-apply

Confirmed on the Samsung Galaxy Book 10.6 (SM-W620), postmarketOS Plasma 6.6 Wayland. This is the recipe, not more research.

## What the machine needs

| Layer | Home value | Notes |
| --- | --- | --- |
| Picture | KScreen `left` / KWin `Rotated90` | Native panel is 1280×1920. Kernel quirk `RIGHT_UP` makes KWin prefer `Rotated270`, which is the other landscape (upside down for this hold). |
| Finger | KWin `orientationDBus=8` on `STMD1234:00 06CB:1058` | Device-space 180°. Same value at none / left / inverted / right. |
| Pen | KWin `orientationDBus=8` on `WCOM0028:00 2D1F:000C Stylus` | Same residual as touch. |
| Arrow | unchanged | Not inverted on this chassis. |
| Type cover | do not touch | `HID 04e8:a00a` mouse / touchpad / keyboard. Relative pointer; not part of the 180° gap. |

Follow is required even though `R` is constant. KWin **zeros** digitizer `Orientation` on every output transform change, including the 15s Display Configuration revert. The helper only writes `8` on the two names above. How that helper is allowed to run is the rest of this section — a naive poller will cook the m3-7Y30.

## Re-run after a reboot

From a graphical session (or SSH with the session bus), use the C++ binary — not a leftover Python helper:

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export WAYLAND_DISPLAY=wayland-0
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus

tiltback --save-home
tiltback --install-follow
systemctl --user enable --now tiltback-follow.service
```

Or the dashboard: Picture **Left**, Finger/Pen **R=8**, Save home, Install follow. `--install-follow` writes `/usr/bin/tiltback --follow` when that file exists, and stops a leftover `tiltback-w620.service` if one is still enabled. Do not also autostart `--follow` from Plasma.

## Follow: what burned the CPU, and the fix

The first helper was not a tight spin loop. It slept 0.4s, then did an expensive amount of work, and a bad install ran **three copies** of that at once (a systemd unit plus two Plasma autostart `.desktop` files).

Each copy, every 0.4s, forked `kscreen-doctor -j` (a full Qt/KScreen dump). Every 2s it also ran `busctl tree` and about six `busctl get-property` calls on **all** KWin input nodes (fourteen on this chassis, including the type cover) just to see if two digitizers still had `R=8`. That is the ~80% spike every couple of seconds.

KWin only wipes `R` when `T` changes. Persist (`kcminputrc`) is not enough; follow is still required. What is *not* required is a process storm.

The current recipe (`tiltback --follow`):

- One `tiltback-follow.service`. `--install-follow` stops and disables a leftover Python `tiltback-w620.service` if it is still present.
- No `kscreen-doctor` and no `busctl` in the follow loop. In-process `QDBus` `Get`/`Set` on the two named digitizers only.
- Wake sources: KWin `PropertiesChanged`, a **directory** watch on `~/.config` / `~/.local/share/kscreen` (KWin replaces the `kwinoutputconfig.json` inode; a watch on the file itself goes silent), and a 0.4s tick that only Gets `orientationDBus` on the cached stylus and touch paths. If `R` is already 8, the tick does nothing else.
- The 0.4s tick exists because KWin often zeros `R` in memory **without** a D-Bus notify, and because a read-only root (`emergency_ro` after an `sda` write error on 2026-09-21) means the config file is never rewritten. Without the tick, follow waited 30s for a “safety” restamp. The tick is the delay killer; it is not the old poller.
- Sleep: `PrepareForSleep` stops the tick and all Gets/Sets. After resume, follow waits 2.5s (and skips `Set` if `Get` fails) so it does not race KWin `applyScreenToDevice`. Fedora 44 KWin 6.7 `setOrientation` from D-Bus during that apply aborted `libinput-connection` (`std::bad_alloc`), which killed the compositor — every key including power and volume — until `kwin_wayland_wrapper` restarted. Input D-Bus calls use an 800ms timeout, not the 25s default.

If home is emergency-RO, stage the binary at `/tmp/tiltback` and use the runtime systemd drop-in; `--install-follow` already knows that path.

Useful flags:

```sh
tiltback --report          # four layers, no writes
tiltback --save-home       # persist the measured tuple
tiltback --install-follow  # one user unit
tiltback --follow          # daemon (systemd only)
```

After a clean reboot, persisted `kwinoutputconfig.json` (`Rotated90`) and `kcminputrc` (`Orientation=8`) may already be enough for the picture. The service still has to re-stamp `R=8` if KWin drops it at login. If the desktop comes up on the wrong landscape, apply Left + R=8 from the dashboard again.

## Type cover touchpad (separate from TiltBack)

After disconnect/reconnect of USB `04e8:a00a`, the keyboard (HID interface 1.1, `hid-generic`) comes back and the touchpad (interface 1.0, `hid-multitouch`) does not. Fn+F5 and Plasma’s enable toggle cannot fix it. This is not the orientation helper.

KWin can show `enabled=true` and `kcminputrc` `Enabled=true` while the pad is still dead. Before the unplug, libinput was processing `event3` (taps and motion). After replug at 03:54:53, the kernel logged:

```text
usb 1-5: descriptor type invalid, skip
hid-multitouch 0003:04E8:A00A.0006: failed to fetch feature 8
```

and then **no further touch events**. The same `failed to fetch feature 8` also appears on a working cold plug, so it is a symptom of this cover, not a complete explanation. The distinguishing fact is: after hotplug the multitouch collection never streams.

This is a known Galaxy Book cover bug (same ID on the 10.6 and 12). Sleep works because resume **power-cycles the xHCI port**. `echo 0/1 > …/authorized` only logically detaches the device; that is why it did not recover the pad.

### What a real recovery can be

Three layers, cheapest first. None of these belong in TiltBack.

1. **Manual, proven:** suspend and wake. That is the control experiment.

2. **Userspace, root — tried, did not recover the pad.** After a live dead reconnect on this chassis:

   - `USBDEVFS_RESET`: kernel `usb 1-5: reset full-speed USB device number N`. Same address, hid-multitouch stayed bound, no touch events.
   - sysfs `port/disable` 0/1 and hub `PORT_POWER` (VBUS): real disconnect and a new address (6→7, then 7→8). Same `descriptor type invalid, skip` and `failed to fetch feature 8` as a physical replug. Six-second evdev sample on Mouse + Touchpad + keyboard: **0 events**.

   So a port reset is **not** what sleep does for this cover. `authorized` 0/1 is weaker still. A udev-on-add reset would only replay the dead hotplug. Do not ship a reset helper.

   hid-multitouch unbind/bind after the device has been up for minutes still fails feature 8 and does not start the stream.

3. **Kernel, the durable fix:** `hid-multitouch` GET_FEATURE report 8 (`failed to fetch feature 8`) is the driver asking the cover for a Win8/PTP feature (contact count / input mode). On hotplug the gadget is not ready (`descriptor type invalid, skip` on the same device). A quirk for `04e8:a00a` should **delay and retry** that GET, or skip it and SET the input mode the way a resume probe eventually does. That is a kernel patch, not a Plasma setting.

Fn+F5 and the Touchpad KCM stay irrelevant: after hotplug there are no events to enable.

## What we are not wrapping

- No kernel cmdline / quirk patch. Upstream `RIGHT_UP` stays live. Userspace home is `Rotated90` + `R=8`.
- No udev rules, no root, no `input` group.
- No IMU / auto-rotate. There is no IIO device.
- No cursor workaround.
- Follow must not poll `kscreen-doctor`. One `tiltback-follow.service`. In-process D-Bus + directory watch + a 0.4s Get of two orientations. See above.
