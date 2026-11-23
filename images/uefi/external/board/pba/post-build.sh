#!/bin/sh
# Post-build for the PBA root filesystem. $1 is the target directory.
set -e
TARGET="$1"

# Nothing that can open a login session or a shell for a user.
for f in bin/login sbin/getty sbin/sulogin usr/bin/su bin/su usr/sbin/telnetd usr/sbin/httpd; do
	rm -f "$TARGET/$f"
done
# Only rcS runs at start-up (see the overlay); drop the default services.
find "$TARGET/etc/init.d" -name 'S[0-9]*' -exec rm -f {} +
rm -f "$TARGET/etc/init.d/rcK"

# Root account locked in /etc/shadow ("*" means no password can match).
sed -i 's/^root:[^:]*:/root:*:/' "$TARGET/etc/shadow"
grep -q '^root:\*:' "$TARGET/etc/shadow"

# The PBA program must be there.
test -x "$TARGET/usr/sbin/linuxpba"
