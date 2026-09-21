#!/bin/sh
# Native host packages: .deb / .rpm / .pkg.tar.zst into dist/.
# Greeter persist and the GNOME HID helper live in /usr; ~/.local cannot
# enable those system units. Use --local only for a no-root session clinic.
# postmarketOS musl from a glibc PC: ./scripts/build-apk.sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
pkgver=0.0.0
mode=package
for arg in "$@"; do
    case $arg in
        --local)
            mode=local
            ;;
        -h|--help)
            echo "usage: $0 [--local]"
            echo "  default  build distro packages into dist/"
            echo "  --local  cmake --install to PREFIX (default ~/.local); no greeter units"
            echo "  PREFIX   used only with --local"
            exit 0
            ;;
        --install)
            echo "$0: --install is gone. Default is a distro package in dist/." >&2
            echo "  $0           # .rpm / .deb / .pkg.tar.zst" >&2
            echo "  $0 --local   # ~/.local session clinic only" >&2
            exit 2
            ;;
        *)
            echo "unknown argument: $arg" >&2
            echo "usage: $0 [--local]" >&2
            exit 2
            ;;
    esac
done

if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
else
    ID=unknown
    ID_LIKE=
fi
id=$(printf '%s' "${ID:-unknown}" | tr '[:upper:]' '[:lower:]')
like=$(printf '%s' "${ID_LIKE:-}" | tr '[:upper:]' '[:lower:]')
family=unknown
for token in "$id" $like; do
    case $token in
        debian|ubuntu|linuxmint|pop|raspbian|kali)
            family=debian
            break
            ;;
        fedora|rhel|centos|rocky|almalinux|ol)
            family=fedora
            break
            ;;
        arch|manjaro|endeavouros|garuda)
            family=arch
            break
            ;;
        alpine)
            family=alpine
            break
            ;;
        opensuse*|sles|suse)
            family=suse
            break
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

cmake_configure() {
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
}

need_cmd() {
    if command -v "$1" >/dev/null 2>&1; then
        return 0
    fi
    echo "missing $1 — $2" >&2
    exit 1
}

tarball() {
    dest=$1
    tar -C "$root" \
        --exclude=.git --exclude=build --exclude=build-alpine \
        --exclude=packaging/alpine/work --exclude=packaging/alpine/packages \
        --exclude=packaging/rpm/work --exclude=dist --exclude=debian \
        --transform "s,^,tiltback-$pkgver/," \
        -czf "$dest" .
}

build_rpm() {
    need_cmd rpmbuild "sudo dnf install rpm-build   (openSUSE: sudo zypper install rpm-build)"
    work=$root/packaging/rpm/work
    rm -rf "$work"
    mkdir -p "$work/SOURCES" "$work/SPECS" "$root/dist"
    tarball "$work/SOURCES/tiltback-$pkgver.tar.gz"
    cp "$root/packaging/rpm/tiltback.spec" "$work/SPECS/"
    rpmbuild -D "_topdir $work" -ba "$work/SPECS/tiltback.spec"
    find "$work/RPMS" "$work/SRPMS" -name '*.rpm' -exec cp -a {} "$root/dist/" \;
}

build_deb() {
    need_cmd dpkg-buildpackage "sudo apt install dpkg-dev debhelper"
    src=$root/dist/deb-src/tiltback-$pkgver
    rm -rf "$root/dist/deb-src"
    mkdir -p "$src"
    tar -C "$root" \
        --exclude=.git --exclude=build --exclude=build-alpine \
        --exclude=packaging/alpine/work --exclude=packaging/alpine/packages \
        --exclude=packaging/rpm/work --exclude=dist --exclude=debian \
        -cf - . | tar -C "$src" -xf -
    cp -a "$root/packaging/debian" "$src/debian"
    (cd "$src" && dpkg-buildpackage -us -uc -b)
    mkdir -p "$root/dist"
    find "$root/dist/deb-src" -maxdepth 1 -name '*.deb' -exec cp -a {} "$root/dist/" \;
}

build_arch() {
    need_cmd makepkg "sudo pacman -S --needed base-devel"
    mkdir -p "$root/dist"
    (cd "$root/packaging/arch" && makepkg -f)
    find "$root/packaging/arch" -maxdepth 1 -name 'tiltback*.pkg.tar.zst' -exec cp -a {} "$root/dist/" \;
}

if [ "$mode" = local ]; then
    cmake_configure
    prefix=${PREFIX:-$HOME/.local}
    cmake --install "$root/build" --prefix "$prefix"
    bin="$prefix/bin/tiltback"
    desktop="$prefix/share/applications/org.tiltback.TiltBack.desktop"
    icons="$prefix/share/icons/hicolor"
    case ":$PATH:" in
        *:"$prefix/bin":*) ;;
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
        command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f "$icons" 2>/dev/null || true
    fi
    command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 --noincremental 2>/dev/null || true
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$prefix/share/applications" 2>/dev/null || true
    echo "installed: $bin  (session only; greeter persist needs a /usr package)"
    echo "desktop:   $desktop"
    echo "next:      $bin --install-follow && systemctl --user enable --now tiltback-follow.service"
    exit 0
fi

mkdir -p "$root/dist"
echo "distro ${PRETTY_NAME:-$id} ($family) — packages in $root/dist"

case $family in
    fedora|suse)
        build_rpm
        ;;
    debian)
        build_deb
        ;;
    arch)
        build_arch
        ;;
    alpine)
        echo "Alpine/pmOS musl packages: ./scripts/build-apk.sh"
        exec "$root/scripts/build-apk.sh"
        ;;
    *)
        echo "unknown distro — cannot pick .deb / .rpm / .pkg.tar.zst" >&2
        echo "  --local  still installs a session clinic to ~/.local" >&2
        exit 1
        ;;
esac

echo "packages:"
find "$root/dist" -maxdepth 1 \( -name '*.rpm' -o -name '*.deb' -o -name '*.pkg.tar.zst' -o -name '*.apk' \) -print | sort
echo "install with the distro tool (dnf/zypper/apt/pacman/apk). Use /usr/bin/tiltback."
echo "GNOME: also install tiltback-gnome. Then Save home + Install follow in the GUI."
