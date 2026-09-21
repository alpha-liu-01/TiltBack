#!/bin/sh
# Mutter keeps evdev open. udevadm trigger does not reload calibration.
# Installed as /usr/libexec/tiltback/rebind-hid.sh (package) or copied to
# ~/.config/tiltback/rebind-hid.sh (unpackaged clinic).
set -eu

RULES=/etc/udev/rules.d/61-tiltback.rules
if [ ! -e "$RULES" ]; then
	for f in /home/*/.config/tiltback/61-tiltback.rules; do
		[ -f "$f" ] || continue
		RULES=$f
		break
	done
fi

STAMP_RUN=/run/tiltback/last-rebind-done
mkdir -p /run/tiltback

udevadm control --reload || true
rebound=0
for hid in /sys/bus/hid/devices/*; do
	[ -e "$hid/driver" ] || continue
	id=$(basename "$hid")
	case "$id" in
	*:04E8:A00A.*) continue ;;
	*:06CB:1058.* | *:2D1F:000C.*) ;;
	*) continue ;;
	esac
	drv=$(readlink -f "$hid/driver")
	echo "$id" >"$drv/unbind"
	echo "$id" >"$drv/bind"
	rebound=$((rebound + 1))
done
[ "$rebound" -gt 0 ]

if [ -e "$RULES" ]; then
	hash=$(sha256sum "$RULES" | awk '{print $1}')
else
	hash=none
fi
printf '%s\n' "$hash" >"$STAMP_RUN"
chmod 644 "$STAMP_RUN" || true
if [ -e "$RULES" ]; then
	target=$(readlink -f "$RULES")
	if [ -n "$target" ] && [ -w "$(dirname "$target")" ]; then
		printf '%s\n' "$hash" >"$(dirname "$target")/last-rebind-done"
	fi
fi
