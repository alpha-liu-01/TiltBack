---
name: Phase 0 skeleton
overview: Create a musl-linked Qt 6 Quick + C++ skeleton, build it in an Alpine container on this machine (the W620 is too slow to compile), and install a `.desktop` launcher on the postmarketOS 26.06 tablet so a thumb can open an empty window on Plasma Wayland.
todos:
  - id: cmake-qml
    content: "Add CMake Qt6 Quick project: src/main.cpp, qml/Main.qml (maximized empty page + corner labels), org.tiltback.TiltBack.desktop"
    status: completed
  - id: alpine-docker
    content: Add Alpine 3.22 musl Dockerfile + scripts/build-alpine.sh (W620 is musl; host glibc will not run)
    status: completed
  - id: deploy-w620
    content: Build locally, scp binary + desktop file to the W620, launch on Plasma Wayland and confirm the empty window
    status: completed
isProject: false
---

# Phase 0 — C++/QML app skeleton

Phase 0 is only a window a thumb can hit. No D-Bus, no probe, no follow, no four cards. Those start in Phase 1.

The W620 is postmarketOS 26.06 / Alpine **musl** x86_64. A glibc binary from this host will not run there. Compile in an Alpine container here, then copy the binary to the tablet.

```mermaid
flowchart LR
  host[This_Linux_host]
  docker[Alpine_musl_container]
  w620[W620_Plasma_Wayland]
  host --> docker
  docker -->|scp_binary| w620
```

## What to add

New app tree only. Leave [tools/tiltback-w620](tools/tiltback-w620) and the docs recipes alone.

- [CMakeLists.txt](CMakeLists.txt) — C++17, `find_package(Qt6 REQUIRED COMPONENTS Quick)`, `qt_add_executable(tiltback)`, `qt_add_qml_module` so QML is compiled into the binary (one file to copy). Link **Quick** only. Do not add DBus, Widgets, or a backend library yet; Phase 1 will add `Qt6::DBus`.
- [src/main.cpp](src/main.cpp) — `QGuiApplication` + `QQmlApplicationEngine` loading `qrc:/qt/qml/TiltBack/Main.qml`. Set `QGuiApplication::setDesktopFileName` / organization to `org.tiltback.TiltBack` so Plasma can match the `.desktop` file.
- [qml/Main.qml](qml/Main.qml) — `ApplicationWindow`, title TiltBack, one empty page: large title plus a short “Phase 0 — no backends” line. `visibility: Window.Maximized` (not a calibration overlay). Four huge corner labels so the window is still obvious if the picture is sideways. No settings, no apply.
- [data/org.tiltback.TiltBack.desktop](data/org.tiltback.TiltBack.desktop) — `Exec=tiltback`, `Terminal=false`, `Categories=Utility;`, `X-KDE-Wayland-Interfaces` unset. Installable to `~/.local/share/applications/` on the tablet.
- [scripts/build-alpine.sh](scripts/build-alpine.sh) + [docker/alpine-qt6.Dockerfile](docker/alpine-qt6.Dockerfile) — Alpine 3.22 (pmOS 26.06’s userland) with `cmake`, `ninja`, `g++`, `qt6-qtbase-dev`, `qt6-qtdeclarative-dev`. Bind-mount the repo, `cmake -G Ninja -S . -B build-alpine`, produce `build-alpine/tiltback`.
- [.gitignore](.gitignore) — `build/`, `build-alpine/`.
- A short “how to build / install on the W620” subsection in [docs/concept-and-feasibility.md](docs/concept-and-feasibility.md) under Phase 0, or a few lines in [docs/w620-runbook.md](docs/w620-runbook.md). Not a second architecture doc.

## Install and prove on the W620

1. `./scripts/build-alpine.sh`
2. `scp` the musl `tiltback` binary to `~/.local/bin/tiltback` (if root is still `emergency_ro`, stage under `/tmp` and `~/.local/bin` when the disk is writable).
3. Install the `.desktop` file into `~/.local/share/applications/`.
4. From a graphical session (or SSH with `XDG_RUNTIME_DIR` / `WAYLAND_DISPLAY` / session bus), run `tiltback` and confirm a maximized window on `wayland-0`.
5. Confirm it also appears in the Plasma app launcher.

Done when: one C++ process, no Python, an empty thumb-sized window on the W620, and a containerized build that another machine can repeat.

## Out of Phase 0

- `QDBus`, KScreen, KWin InputDevice, DRM `panel-orientation`
- Follow helper, apply/revert, wizard, Arrow
- System package / apk recipe, icons, translations
- Replacing [tools/tiltback-w620](tools/tiltback-w620)
