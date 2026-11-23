# Shown when the Rescue shell starts.
echo "sedutil-rescue: ready" > /dev/ttyS0 2>/dev/null
cat <<'TXT'

  sedutil Rescue
  --------------
  sedutil-cli --scan                              list drives and Opal support
  sedutil-cli --query DRIVE                       show a drive's lock state
  sedutil-cli --setLockingRange 0 RW - DRIVE      unlock a drive by hand
  sedutil-cli --setMBRDone on - DRIVE             show the real disk instead of the PBA
  sedutil-cli --setMBREnable off - DRIVE          turn the PBA off on a drive
  sedutil-cli --loadPBAimage - FILE DRIVE         load a PBA image

  "-" asks for the password instead of taking it on the command line.
  DRIVE is /dev/nvme0, /dev/sda and so on.
  reboot -f                                       restart

  Your keyboard is set to the US layout. Type loadkeys <layout> to change it.

TXT
