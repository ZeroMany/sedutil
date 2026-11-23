#!/bin/bash
# Build the sedutil UEFI PBA or Rescue image with Buildroot.
#
#   images/uefi/build.sh pba|rescue [work-dir]
#
# Downloads Buildroot (pinned below), builds this repository's sedutil into
# it, and leaves the images in <work-dir>/<kind>/images/:
#   sedutil-<kind>-uefi64.img   disk image (load with --loadPBAimage, or
#                               write to a USB stick)
#   BOOTX64-<kind>.EFI          the kernel itself, for signing or inspection
#
# work-dir defaults to ../sedutil-build next to the repository; it must be
# outside the repository, because Buildroot copies the repository as the
# sedutil source.
set -euo pipefail

BUILDROOT_VERSION=2026.02.3
BUILDROOT_URL=https://gitlab.com/buildroot.org/buildroot.git

KIND="${1:-}"
case "$KIND" in
	pba|rescue) ;;
	*) echo "usage: $0 pba|rescue [work-dir]" >&2; exit 2 ;;
esac

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="${2:-$REPO/../sedutil-build}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
case "$WORK/" in
	"$REPO"/*) echo "work-dir must be outside the repository" >&2; exit 2 ;;
esac

BR="$WORK/buildroot-$BUILDROOT_VERSION"
if [ ! -d "$BR" ]; then
	git clone --quiet --depth 1 --branch "$BUILDROOT_VERSION" "$BUILDROOT_URL" "$BR.tmp"
	mv "$BR.tmp" "$BR"
fi

OUT="$WORK/$KIND"
mkdir -p "$OUT"
# Use this repository as the sedutil source instead of the release tarball.
cat > "$OUT/local.mk" <<MK
SEDUTIL_OVERRIDE_SRCDIR = $REPO
SEDUTIL_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = --exclude images --exclude windows --exclude freebsd --exclude debian
MK

VERSION="$(git -C "$REPO" describe --tags --always --dirty 2>/dev/null || echo unknown)"
export BR2_DL_DIR="${BR2_DL_DIR:-$WORK/dl}"

make -C "$BR" O="$OUT" BR2_EXTERNAL="$HERE/external" "sedutil_${KIND}_defconfig"
make -C "$BR" O="$OUT" SEDUTIL_PBA_VERSION="$VERSION"

ls -l "$OUT/images/"
