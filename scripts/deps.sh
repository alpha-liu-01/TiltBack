#!/bin/sh
# Print the host package line for a native TiltBack build, then check
# that cmake / a C++ compiler / pkg-config / Qt6 Quick+Controls2+DBus /
# libdrm are present. Does not invoke sudo.
#
# Ninja is preferred (see scripts/build.sh) but not required.
set -eu

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

in_list() {
    needle=$1
    shift
    for item in "$@"; do
        [ "$item" = "$needle" ] && return 0
    done
    return 1
}

# ID first, then any ID_LIKE token.
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

case $family in
    debian)
        pkgs='sudo apt install cmake ninja-build g++ pkg-config libdrm-dev qt6-base-dev qt6-declarative-dev libx11-dev libxrandr-dev libxi-dev dpkg-dev debhelper'
        ;;
    fedora)
        pkgs='sudo dnf install cmake ninja-build gcc-c++ pkgconf-pkg-config libdrm-devel qt6-qtbase-devel qt6-qtdeclarative-devel libX11-devel libXrandr-devel libXi-devel rpm-build'
        ;;
    arch)
        pkgs='sudo pacman -S --needed cmake ninja gcc pkgconf libdrm qt6-base qt6-declarative libx11 libxrandr libxi base-devel'
        ;;
    alpine)
        pkgs='sudo apk add cmake ninja g++ pkgconf libdrm-dev qt6-qtbase-dev qt6-qtdeclarative-dev'
        x11_pkgs='libx11-dev libxrandr-dev libxi-dev'
        ;;
    suse)
        pkgs='sudo zypper install cmake ninja gcc-c++ pkgconf-pkg-config libdrm-devel qt6-base-devel qt6-declarative-devel libX11-devel libXrandr-devel libXi-devel rpm-build'
        ;;
    *)
        pkgs='# unknown distro — need cmake, a C++ compiler, pkg-config, libdrm, Qt 6 Quick + QuickControls2 + DBus'
        ;;
esac

echo "distro  ${PRETTY_NAME:-$id} ($family)"
echo "install $pkgs"
echo
echo "KWin (Plasma Wayland) is the default backend. X11/XFCE needs libX11 + libXrandr + libXi at build time (recommended, not required)."
if [ "$family" = alpine ]; then
    echo "Alpine native is fine on this machine. For postmarketOS musl from a glibc host, use ./scripts/build-alpine.sh"
    echo "X11 on Alpine (optional): sudo apk add ${x11_pkgs:-libx11-dev libxrandr-dev libxi-dev}"
    echo "Missing X11 libs is not a hard fail — the musl image stays KWin-only."
fi

missing=0

need_cmd() {
    if command -v "$1" >/dev/null 2>&1; then
        echo "ok    $1  $($1 --version 2>/dev/null | head -n 1)"
        return 0
    fi
    echo "miss  $1"
    missing=1
}

need_any() {
    label=$1
    shift
    for c in "$@"; do
        if command -v "$c" >/dev/null 2>&1; then
            echo "ok    $label  $($c --version 2>/dev/null | head -n 1)"
            return 0
        fi
    done
    echo "miss  $label  (tried: $*)"
    missing=1
}

need_cmd cmake
need_any "C++ compiler" g++ c++ clang++
need_any pkg-config pkg-config pkgconf

if command -v ninja >/dev/null 2>&1; then
    echo "ok    ninja  $(ninja --version 2>/dev/null)  (used by ./scripts/build.sh)"
else
    echo "opt   ninja  missing — ./scripts/build.sh will use Unix Makefiles"
fi

if command -v pkg-config >/dev/null 2>&1 || command -v pkgconf >/dev/null 2>&1; then
    pc=pkg-config
    command -v pkg-config >/dev/null 2>&1 || pc=pkgconf
    if $pc --exists libdrm; then
        echo "ok    libdrm  $($pc --modversion libdrm)"
    else
        echo "miss  libdrm  (pkg-config)"
        missing=1
    fi
    if $pc --exists x11 && $pc --exists xrandr && $pc --exists xi; then
        echo "ok    X11     x11 $($pc --modversion x11), xrandr $($pc --modversion xrandr), xi $($pc --modversion xi)"
    else
        echo "opt   X11     missing x11/xrandr/xi — KWin-only build; install the X11 -dev packages for XFCE/Xorg"
    fi
fi

has_qt_cmake() {
    name=$1
    for base in /usr/lib/cmake /usr/lib64/cmake /usr/local/lib/cmake /usr/local/lib64/cmake; do
        [ -f "$base/$name/${name}Config.cmake" ] && return 0
    done
    for f in /usr/lib/*/cmake/"$name"/"${name}Config.cmake"; do
        [ -f "$f" ] && return 0
    done
    return 1
}

for mod in Qt6Quick Qt6QuickControls2 Qt6DBus; do
    if has_qt_cmake "$mod"; then
        echo "ok    $mod  (CMake)"
    else
        echo "miss  $mod  (CMake ${mod}Config.cmake)"
        missing=1
    fi
done

if [ "$missing" -ne 0 ]; then
    echo
    echo "missing build deps — run the install line above, then re-run $0"
    exit 1
fi

echo
echo "deps ok"
