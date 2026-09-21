#!/bin/sh
# Build Alpine .apk files (tiltback + tiltback-gnome) in the musl Docker image.
# Output: packaging/alpine/packages/x86_64/*.apk
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
pkgver=0.0.0
image=tiltback-alpine-apk
work="$root/packaging/alpine/work"
tarball="tiltback-$pkgver.tar.gz"

rm -rf "$work"
mkdir -p "$work/src" "$root/packaging/alpine/packages"

# Working tree, not git archive — packaging must include uncommitted clinic files.
tar -C "$root" --exclude=.git --exclude=build --exclude=build-alpine \
	--exclude=packaging/alpine/work --exclude=packaging/alpine/packages \
	--exclude=.cache \
	--transform "s,^,tiltback-$pkgver/," \
	-czf "$work/src/$tarball" .

cp "$root/packaging/alpine/APKBUILD" \
	"$root/packaging/alpine/tiltback.post-install" \
	"$root/packaging/alpine/tiltback.post-upgrade" \
	"$root/packaging/alpine/tiltback-gnome.post-install" \
	"$root/packaging/alpine/tiltback-gnome.post-upgrade" \
	"$root/packaging/alpine/tiltback-gnome.pre-deinstall" \
	"$work/src/"

# abuild refuses SKIP in CI; checksum the tarball we just wrote.
docker build -t "$image" --build-arg "UID=$(id -u)" \
	-f "$root/docker/alpine-apk.Dockerfile" "$root/docker"

docker run --rm \
	-v "$work/src:/src:rw" \
	-v "$root/packaging/alpine/packages:/packages:rw" \
	-w /src \
	"$image" \
	sh -ceu '
		abuild checksum
		abuild -P /packages -r
		find /packages -name "*.apk" -print
	'

echo "apks under $root/packaging/alpine/packages"
