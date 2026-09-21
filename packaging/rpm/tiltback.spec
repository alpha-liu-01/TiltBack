# Weak dep: Fedora/openSUSE install tiltback-gnome when gnome-shell is present.
# %package gnome
# Supplements: (tiltback and gnome-shell)
# Requires: tiltback = %{version}-%{release}

Name:           tiltback
Version:        0.0.0
Release:        1%{?dist}
Summary:        Tablet orientation clinic
License:        LicenseRef-None-Declared
URL:            https://github.com/alpha-liu-01/TiltBack

%description
Qt Quick clinic for tablet picture and digitizer residuals.

%package gnome
Summary:        GNOME/Mutter HID rebind helper for TiltBack
Requires:       %{name} = %{version}-%{release}
Supplements:    (%{name} and gnome-shell)

%description gnome
Systemd path unit that rebinds the built-in digitizers after TiltBack
writes a udev calibration matrix. Mutter does not reopen evdev on
udevadm trigger.

%post gnome
if [ -d /run/systemd/system ]; then
	systemd-tmpfiles --create %{_prefix}/lib/tmpfiles.d/tiltback.conf >/dev/null 2>&1 || :
	%systemd_post tiltback-rebind.path
	systemctl enable --now tiltback-rebind.path >/dev/null 2>&1 || :
fi

%preun gnome
%systemd_preun tiltback-rebind.path

%postun gnome
%systemd_postun_with_restart tiltback-rebind.path
