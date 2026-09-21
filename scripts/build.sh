#!/bin/sh
# Native CMake build for a glibc (or local musl) Plasma host.
# For postmarketOS from a glibc machine, use ./scripts/build-alpine.sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
do_install=0
for arg in "$@"; do
    case $arg in
        --install)
            do_install=1
            ;;
        -h|--help)
            echo "usage: $0 [--install]"
            echo "  PREFIX  install prefix (default: \$HOME/.local)"
            exit 0
            ;;
        *)
            echo "unknown argument: $arg" >&2
            echo "usage: $0 [--install]" >&2
            exit 2
            ;;
    esac
done

if command -v ninja >/dev/null 2>&1; then
    gen='Ninja'
    ninja_bin=$(command -v ninja)
else
    gen='Unix Makefiles'
    ninja_bin=
fi

# A previous configure can pin CMAKE_MAKE_PROGRAM to a deleted path
# (e.g. a user sysroot). Wipe the cache so we pick the system ninja.
if [ -f "$root/build/CMakeCache.txt" ]; then
    cached=$(sed -n 's/^CMAKE_MAKE_PROGRAM:FILEPATH=//p' "$root/build/CMakeCache.txt" | head -n 1)
    if [ -n "$cached" ] && [ ! -x "$cached" ]; then
        echo "stale CMAKE_MAKE_PROGRAM=$cached — wiping $root/build"
        rm -rf "$root/build"
    fi
fi

mkdir -p "$root/build"
cmake_args="-DCMAKE_BUILD_TYPE=Release"
if [ -n "$ninja_bin" ]; then
    cmake_args="$cmake_args -DCMAKE_MAKE_PROGRAM=$ninja_bin"
fi
# shellcheck disable=SC2086
cmake -G "$gen" -S "$root" -B "$root/build" $cmake_args
cmake --build "$root/build"

echo "binary: $root/build/tiltback"

if [ "$do_install" -eq 0 ]; then
    echo "not installed — re-run with --install (PREFIX=${PREFIX:-$HOME/.local})"
    exit 0
fi

prefix=${PREFIX:-$HOME/.local}
cmake --install "$root/build" --prefix "$prefix"

bin="$prefix/bin/tiltback"
desktop="$prefix/share/applications/org.tiltback.TiltBack.desktop"
icons="$prefix/share/icons/hicolor"

# Plasma launchers often omit ~/.local/bin even when a login shell has it.
case ":$PATH:" in
    *:"$prefix/bin":*)
        ;;
    *)
        if [ -f "$desktop" ]; then
            tmp=$(mktemp)
            sed "s|^Exec=tiltback$|Exec=$bin|" "$desktop" >"$tmp"
            mv "$tmp" "$desktop"
            echo "desktop Exec=$bin  ($prefix/bin is not on PATH)"
        fi
        ;;
esac

if [ -d "$icons" ]; then
    touch "$icons"
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f "$icons" 2>/dev/null || true
    fi
fi
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$prefix/share/applications" 2>/dev/null || true
fi

echo "installed: $bin"
echo "desktop:   $desktop"
echo "next:      $bin --install-follow && systemctl --user enable --now tiltback-follow.service"
