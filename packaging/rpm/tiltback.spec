# Weak dep: Fedora/openSUSE install tiltback-gnome when gnome-shell is present.
# Source tarball: from the repo root,
#   tar --exclude=.git --exclude=build --exclude=build-alpine \
#       --exclude=packaging/alpine/work --exclude=packaging/alpine/packages \
#       -czf tiltback-0.0.0.tar.gz --transform 's,^,tiltback-0.0.0/,' .

Name:           tiltback
Version:        0.0.0
Release:        1%{?dist}
Summary:        Tablet orientation clinic
License:        GPL-3.0-or-later
URL:            https://github.com/alpha-liu-01/TiltBack
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconf
BuildRequires:  libdrm-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  libX11-devel
BuildRequires:  libXrandr-devel
BuildRequires:  libXi-devel
BuildRequires:  systemd-rpm-macros

Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtwayland
Suggests:       %{name}-gnome

%description
Qt Quick clinic for tablet picture and digitizer residuals.

%package gnome
Summary:        GNOME/Mutter HID rebind helper for TiltBack
Requires:       %{name} = %{version}-%{release}
Requires:       systemd
Supplements:    (%{name} and gnome-shell)

%description gnome
Ships /usr/libexec/tiltback/rebind-hid.sh and enables
tiltback-rebind.path so follow can reopen evdev after a udev
calibration write without a password prompt. Mutter does not
reopen evdev on udevadm trigger.

%prep
%autosetup

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%post
# Leftover clinic desktop under ~/.local wins over /usr/share.
for f in /home/*/.local/share/applications/org.tiltback.TiltBack.desktop; do
	[ -f "$f" ] || continue
	if grep -q '\.local/bin/tiltback' "$f"; then
		rm -f "$f"
	fi
done
for b in /home/*/.local/bin/tiltback; do
	[ -x "$b" ] || continue
	mv "$b" "$b.pre-pkg" 2>/dev/null || :
done
for u in /home/*/.config/systemd/user/tiltback-follow.service; do
	[ -f "$u" ] || continue
	sed -i 's|/home/[^/]*/.local/bin/tiltback|/usr/bin/tiltback|g' "$u" 2>/dev/null || :
done
if [ -d /run/systemd/system ]; then
	systemd-tmpfiles --create %{_prefix}/lib/tmpfiles.d/tiltback.conf >/dev/null 2>&1 || :
	%systemd_post tiltback-greeter.service tiltback-greeter.path
	systemctl enable --now tiltback-greeter.service tiltback-greeter.path >/dev/null 2>&1 || :
fi

%preun
%systemd_preun tiltback-greeter.service tiltback-greeter.path

%postun
%systemd_postun tiltback-greeter.service tiltback-greeter.path

%post gnome
if [ -d /run/systemd/system ]; then
	%systemd_post tiltback-rebind.path
	systemctl enable --now tiltback-rebind.path >/dev/null 2>&1 || :
fi

%preun gnome
%systemd_preun tiltback-rebind.path

%postun gnome
%systemd_postun_with_restart tiltback-rebind.path

%files
%license LICENSE
%doc README.md
%{_bindir}/tiltback
%{_datadir}/applications/org.tiltback.TiltBack.desktop
%{_datadir}/icons/hicolor/*/apps/org.tiltback.TiltBack.png
%{_libexecdir}/tiltback/install-greeter.sh
%{_unitdir}/tiltback-greeter.service
%{_unitdir}/tiltback-greeter.path
%{_userunitdir}/tiltback-greeter-follow.service
%{_userunitdir}/plasma-login-wayland.target.wants/tiltback-greeter-follow.service
%{_prefix}/lib/systemd/system-preset/80-tiltback.preset
%{_tmpfilesdir}/tiltback.conf

%files gnome
%{_libexecdir}/tiltback/rebind-hid.sh
%{_unitdir}/tiltback-rebind.service
%{_unitdir}/tiltback-rebind.path
%{_prefix}/lib/systemd/system-preset/81-tiltback-gnome.preset
