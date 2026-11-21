#!/bin/sh
# Ignore Ctrl+C, Ctrl+\ and Ctrl+Z so the password prompt cannot be
# interrupted into a shell.
trap '' INT QUIT TSTP
clear
/sbin/linuxpba 2>/tmp/pbaerror.log
# linuxpba reboots by itself. If it ever exits instead (crash, bug), reboot
# here so boot never continues to a login prompt (root has no password).
sync
reboot -f
