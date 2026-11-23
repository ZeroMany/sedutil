#!/bin/sh
# Post-build for the Rescue root filesystem. $1 is the target directory.
set -e
TARGET="$1"
test -x "$TARGET/usr/sbin/sedutil-cli"
