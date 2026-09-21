#!/bin/sh
# Copy the newest clinic persist into the live greeter. No HID rebind:
# the display manager has not opened evdev yet.
# Installed as /usr/libexec/tiltback/install-greeter.sh
# TILTBACK_GREETER_ROOT= prefix is for dest-detection tests only.
set -eu

ROOT=${TILTBACK_GREETER_ROOT:-}

newest_home_json() {
	latest=
	latest_m=0
	for f in "$ROOT"/home/*/.config/tiltback/home.json; do
		[ -f "$f" ] || continue
		m=$(stat -c %Y "$f" 2>/dev/null || echo 0)
		if [ "$m" -ge "$latest_m" ]; then
			latest_m=$m
			latest=$f
		fi
	done
	printf '%s\n' "$latest"
}

dir_owner() {
	stat -c %U "$1" 2>/dev/null || printf '%s\n' "$2"
}

copy_as() {
	src=$1
	dest=$2
	owner=$3
	[ -f "$src" ] || return 0
	dir=$(dirname "$dest")
	if [ -n "$ROOT" ]; then
		install -d -m 0700 "$dir"
		install -m 0600 "$src" "$dest"
		return 0
	fi
	install -d -m 0700 -o "$owner" -g "$owner" "$dir"
	install -m 0600 -o "$owner" -g "$owner" "$src" "$dest"
	if command -v restorecon >/dev/null 2>&1; then
		restorecon -F "$dir" "$dest" 2>/dev/null || true
	fi
}

install_kwin_greeter() {
	greeter_home=$1
	default_user=$2
	[ -d "$greeter_home" ] || return 0
	owner=$(dir_owner "$greeter_home" "$default_user")
	copy_as "$user_config/kwinoutputconfig.json" \
		"$greeter_home/.config/kwinoutputconfig.json" "$owner"
	copy_as "$user_config/kcminputrc" \
		"$greeter_home/.config/kcminputrc" "$owner"
}

install_mutter_greeter() {
	greeter_home=$1
	default_user=$2
	[ -d "$greeter_home" ] || return 0
	owner=$(dir_owner "$greeter_home" "$default_user")
	copy_as "$user_config/monitors.xml" \
		"$greeter_home/.config/monitors.xml" "$owner"
}

home_json=$(newest_home_json)
[ -n "$home_json" ] || exit 0

user_config=$(dirname "$(dirname "$home_json")")

install_kwin_greeter "$ROOT/var/lib/plasmalogin" plasmalogin
install_kwin_greeter "$ROOT/var/lib/sddm" sddm
install_mutter_greeter "$ROOT/var/lib/gdm" gdm
install_mutter_greeter "$ROOT/var/lib/gdm3" gdm

rules=$user_config/tiltback/61-tiltback.rules
if [ -f "$rules" ]; then
	mkdir -p "$ROOT/etc/udev/rules.d"
	# A symlink into home is unreadable at coldplug if home is late.
	rm -f "$ROOT/etc/udev/rules.d/61-tiltback.rules"
	install -m 0644 "$rules" "$ROOT/etc/udev/rules.d/61-tiltback.rules"
	if [ -z "$ROOT" ]; then
		udevadm control --reload 2>/dev/null || true
	fi
fi

exit 0
