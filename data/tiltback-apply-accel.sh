#!/bin/sh
# Write or remove /etc/udev/rules.d/61-tiltback-accel.rules and reload
# iio-sensor-proxy. Installed as /usr/libexec/tiltback/apply-accel.sh
# Request is a validated payload, not a user udev file.
set -eu

REQ=/run/tiltback/accel-request
RULES=/etc/udev/rules.d/61-tiltback-accel.rules
STAMP=/run/tiltback/last-accel-done

mkdir -p /run/tiltback /etc/udev/rules.d

op=
name=
modalias=
matrix=
nonce=

if [ -f "$REQ" ]; then
	while IFS= read -r line || [ -n "$line" ]; do
		case "$line" in
		op=*) op=${line#op=} ;;
		name=*) name=${line#name=} ;;
		modalias=*) modalias=${line#modalias=} ;;
		matrix=*) matrix=${line#matrix=} ;;
		nonce=*) nonce=${line#nonce=} ;;
		esac
	done <"$REQ"
	# Directory is 1777 sticky. A root-owned 644 request blocks the
	# next clinic write. Leave the file world-writable.
	chmod 666 "$REQ" 2>/dev/null || true
fi

stamp() {
	printf '%s\nnonce=%s\n' "$1" "$nonce" >"$STAMP"
	chmod 644 "$STAMP" || true
}

fail() {
	stamp "error=$1"
	exit 1
}

valid_token() {
	printf '%s' "$1" | grep -Eq '^[A-Za-z0-9:._-]+$'
}

valid_modalias() {
	printf '%s' "$1" | grep -Eq '^[A-Za-z0-9:,._-]+$'
}

valid_matrix() {
	printf '%s' "$1" | awk '
	{
		gsub(/;/, ",")
		n = split($0, a, /[ ,]+/)
		c = 0
		for (i = 1; i <= n; i++) {
			if (a[i] == "")
				continue
			if (a[i] != "-1" && a[i] != "0" && a[i] != "1" \
				&& a[i] != "-1.0" && a[i] != "0.0" && a[i] != "1.0")
				exit 1
			c++
		}
		if (c != 9)
			exit 1
		exit 0
	}'
}

reload_proxy() {
	udevadm control --reload || true
	udevadm trigger -s iio || true
	if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
		systemctl restart iio-sensor-proxy.service || true
	fi
	if command -v restorecon >/dev/null 2>&1 && [ -e "$RULES" ]; then
		restorecon -F "$RULES" 2>/dev/null || true
	fi
	# After restart the last enum is gone. Claim so the property is a
	# real reading before we stamp (flat/Z-dominant may stay undefined).
	if command -v busctl >/dev/null 2>&1; then
		i=0
		while [ "$i" -lt 20 ]; do
			if busctl get-property net.hadess.SensorProxy \
				/net/hadess/SensorProxy net.hadess.SensorProxy \
				HasAccelerometer 2>/dev/null | grep -q true; then
				break
			fi
			i=$((i + 1))
			sleep 0.25
		done
		busctl call net.hadess.SensorProxy /net/hadess/SensorProxy \
			net.hadess.SensorProxy ClaimAccelerometer 2>/dev/null || true
		i=0
		while [ "$i" -lt 20 ]; do
			o=$(busctl get-property net.hadess.SensorProxy \
				/net/hadess/SensorProxy net.hadess.SensorProxy \
				AccelerometerOrientation 2>/dev/null || true)
			case "$o" in
			s\ \"undefined\" | s\ \"\" | "") ;;
			s\ *) break ;;
			esac
			i=$((i + 1))
			sleep 0.25
		done
	fi
}

if [ "$op" = "remove" ]; then
	rm -f "$RULES"
	reload_proxy
	stamp ok
	exit 0
fi

[ "$op" = "apply" ] || fail "unknown op"

case "$name" in
iio:device*) fail "refuse iio:deviceN" ;;
esac

[ -n "$name" ] || [ -n "$modalias" ] || fail "need name or modalias"
if [ -n "$name" ] && ! valid_token "$name"; then
	fail "bad name"
fi
if [ -n "$modalias" ] && ! valid_modalias "$modalias"; then
	fail "bad modalias"
fi
[ -n "$matrix" ] || fail "need matrix"
valid_matrix "$matrix" || fail "bad matrix"

match="SUBSYSTEM==\"iio\", ACTION!=\"remove\""
if [ -n "$name" ]; then
	match="$match, ATTR{name}==\"$name\""
fi
if [ -n "$modalias" ]; then
	match="$match, ATTRS{modalias}==\"$modalias\""
fi

printf '%s, ENV{ACCEL_MOUNT_MATRIX}="%s"\n' "$match" "$matrix" >"$RULES"
chmod 644 "$RULES"
reload_proxy
stamp ok
exit 0
