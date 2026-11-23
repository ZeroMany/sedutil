#!/bin/sh
# Build the disk image from the kernel (initramfs and command line inside).
# Called by Buildroot as: post-image.sh <images dir> <pba|rescue>
set -e
BINARIES_DIR="$1"
KIND="$2"
case "$KIND" in
	pba)    LABEL=SEDUTILPBA; IMAGE=sedutil-pba-uefi64.img ;;
	rescue) LABEL=SEDRESCUE;  IMAGE=sedutil-rescue-uefi64.img ;;
	*) echo "post-image.sh: unknown image kind '$KIND'" >&2; exit 1 ;;
esac
BOARD_DIR="$(dirname "$0")"
CFG="$BINARIES_DIR/genimage-$KIND.cfg"
sed -e "s/@LABEL@/$LABEL/" -e "s/@IMAGE@/$IMAGE/" "$BOARD_DIR/genimage.cfg.in" > "$CFG"

GENIMAGE_TMP="$BUILD_DIR/genimage.tmp"
rm -rf "$GENIMAGE_TMP"
genimage --rootpath "$TARGET_DIR" --tmppath "$GENIMAGE_TMP" \
	--inputpath "$BINARIES_DIR" --outputpath "$BINARIES_DIR" --config "$CFG"

cp "$BINARIES_DIR/bzImage" "$BINARIES_DIR/BOOTX64-$KIND.EFI"
