#!/bin/sh
# Build a musl-linked tiltback for postmarketOS / Alpine x86_64.
# A glibc binary from the host will not run on the W620.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
image=tiltback-alpine-qt6
uid=$(id -u)
gid=$(id -g)

docker build -t "$image" -f "$root/docker/alpine-qt6.Dockerfile" "$root/docker"

mkdir -p "$root/build-alpine"

docker run --rm \
    -u "$uid:$gid" \
    -e HOME=/tmp \
    -e CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}" \
    -v "$root:/src:rw" \
    -w /src \
    "$image" \
    cmake -G Ninja -S . -B build-alpine -DCMAKE_BUILD_TYPE=Release

docker run --rm \
    -u "$uid:$gid" \
    -e HOME=/tmp \
    -e CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}" \
    -v "$root:/src:rw" \
    -w /src \
    "$image" \
    cmake --build build-alpine

echo "musl binary: $root/build-alpine/tiltback"
