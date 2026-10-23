#!/bin/sh
# Run in the dedicated test container with CAP_SYS_ADMIN and loop-device access.
# Only the disposable image below is formatted/mounted; no physical card is used.
set -eu
image=$(mktemp /tmp/leaf-fat-XXXXXX.img)
card=$(mktemp -d /tmp/leaf-fat-card-XXXXXX)
cleanup() {
  mountpoint -q "$card" && umount "$card" || true
  rm -f "$image"
  rmdir "$card"
}
trap cleanup EXIT
truncate -s 512M "$image"
mkfs.fat -F 32 -s 8 "$image"
mount -t vfat -o loop "$image" "$card"
LEAF_TEST_CARD_ROOT="$card" LEAF_TEST_FILL_CARD=1 ./sim/build/logbook_io_test
umount "$card"
fsck.fat -n "$image"
echo 'FAT32-backed logbook tests passed (Linux vfat; not ESP32 power-loss qualification).'
