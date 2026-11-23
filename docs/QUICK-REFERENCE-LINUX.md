# sedutil quick reference (Linux)

Everyday commands for Opal self-encrypting drives, with examples. For Windows, see
[QUICK-REFERENCE-WINDOWS.md](QUICK-REFERENCE-WINDOWS.md).

## Before you start

- Run everything as root (`sudo sedutil-cli ...`, or from the sedutil rescue USB,
  where you're already root).
- Drive names are the whole-drive devices: `/dev/nvme0` for NVMe drives (not
  `/dev/nvme0n1`) and `/dev/sda` for SATA drives (not `/dev/sda1`).
- SATA drives need the kernel option `libata.allow_tpm=1`, or Linux blocks the
  commands sedutil sends. The rescue USB already has it. On a normal Linux
  install, add it to the kernel command line (in `/etc/default/grub` on most
  distributions, then run `update-grub` and restart). NVMe drives don't need it.
- **Type `-` in place of any password** and sedutil asks for it at a hidden prompt.
  A password typed straight into the command line ends up in your shell's history
  file in plain text. When you set a new password, the prompt asks for it twice.

  > **If you typed a password into the command line by mistake**, remove it from
  > your shell's history. In bash:
  >
  > ```bash
  > history | tail       # find the number of the line with the password
  > history -d 123       # delete that line (use its number)
  > history -w           # save the cleaned history to ~/.bash_history
  > ```
  >
  > or clear the whole history with `history -c && history -w`. Do it in the same
  > terminal you typed the password in, and close any other terminals first, so
  > they can't write their own copy back when they exit.

- **Warning: Never lock, erase or revert the drive Linux is running from.**
- Don't use `-n`. It turns off password hashing, and a drive set up without
  hashing can't be unlocked by the boot screen (PBA).
- Back up before anything in the "Changes the drive" sections.

## Look, don't touch (read-only)

| What | Command |
|---|---|
| List drives and Opal support | `sudo sedutil-cli --scan` |
| Show one drive's state | `sudo sedutil-cli --query /dev/nvme0` |
| Is this drive an SED? | `sudo sedutil-cli --isValidSED /dev/nvme0` |
| Match device names to drive models and serial numbers | `lsblk -d -o NAME,MODEL,SERIAL,SIZE` |
| See which partitions are mounted | `lsblk -o NAME,SIZE,MOUNTPOINTS /dev/nvme0n1` |
| Check that the password hashing works | `sedutil-cli --validatePBKDF2` |

In `--scan` output, the `2` column means Opal 2.0, which is what you want. `No`
means the drive can't be managed by sedutil.

In `--query` output, the `Locking function` lines are what matter:

```
Locking function (0x0002)
    Locked = N, LockingEnabled = Y, LockingSupported = Y, MBRDone = Y, MBREnabled = Y, MBRAbsent = N, MediaEncrypt = Y
```

| Field | Meaning |
|---|---|
| `LockingEnabled = Y` | A password protects this drive |
| `Locked = Y` | The drive is locked right now: its data can't be read |
| `MBREnabled = Y` | At power-on the drive shows the boot unlock screen (PBA) instead of its real contents |
| `MBRDone = Y` | The PBA has been passed since power-on, so Linux sees the real contents |

Opal drives re-lock themselves whenever they lose power, so a powered-off
drive is always locked. `Locked = N` on a running PC just means it was unlocked
since power-on, usually by the boot screen.

## Lock and unlock a data drive

The drive locks with your Admin1 password, which is the password you chose at setup.
The examples use the NVMe drive `/dev/nvme0`, whose partitions are
`/dev/nvme0n1p1`, `/dev/nvme0n1p2` and so on. For a SATA drive such as
`/dev/sdb`, the partitions are `/dev/sdb1`, `/dev/sdb2` and so on.

**Lock**

```bash
sudo umount /dev/nvme0n1p1                        # every mounted partition on the drive
sudo sedutil-cli --setLockingRange 0 LK - /dev/nvme0   # prompts for the password
sudo sedutil-cli --query /dev/nvme0               # Optional to verify the drive is locked; should show Locked = Y
```

**Unlock**

```bash
sudo sedutil-cli --setLockingRange 0 RW - /dev/nvme0
sudo sedutil-cli --setMBRDone on - /dev/nvme0     # only if MBREnabled = Y and MBRDone = N
sudo partprobe /dev/nvme0n1                       # Linux re-reads the partitions
sudo mount /dev/nvme0n1p1 /mnt/data               # or let your desktop mount it
```

Notes:

- **Always unmount every partition on the drive before you lock it.** If you
  lock a drive Linux is still using, data Linux hasn't written yet can be lost,
  and the file system can be damaged. If the drive holds swap, run
  `sudo swapoff` on it too.
- `partprobe` comes with the `parted` package. Without it,
  `sudo blockdev --rereadpt /dev/nvme0n1` does the same.
- `--setLockingRange 0 RO` makes the drive read-only instead of locked.

## Changes the drive: passwords and limits

**Change the password.** A drive has two passwords: SID (owner) and Admin1
(locking). `--initialSetup` makes them the same, and the boot screen unlocks
every drive with the one password you type, so keep both passwords the same on all drives.
The boot screen only checks Admin1; SID is needed only for `--setSIDPassword` and
`--revertTPer`. See [Questions and answers](FDE-SETUP-GUIDE.md#10-questions-and-answers) in the setup guide for why keeping them the same is the safer choice.

```bash
sudo sedutil-cli --setSIDPassword - - /dev/nvme0       # current, then new twice
sudo sedutil-cli --setPassword - Admin1 - /dev/nvme0   # current, then new twice
```

**Limit wrong guesses.** By default a drive allows unlimited password attempts.
This command makes it refuse further attempts until it's power-cycled, after the
given number of wrong passwords. It's slower for an attacker, and harmless for you:
switch the PC off and on and try again. Some drives refuse this setting, and
sedutil says so if yours does.

```bash
sudo sedutil-cli --setTryLimit 5 - /dev/nvme0          # 0 removes the limit
```

The setting is permanent: the drive stores it, so it stays through locking,
unlocking, restarts and power cycles until you change it with `--setTryLimit` again.
Removing Opal (`--revertNoErase` or `--revertTPer`) or a PSID erase clears it.
What resets is the count of wrong attempts: it goes back to zero after a correct
password and whenever the drive is power-cycled.

The limit covers the Admin1 password, which is the one that unlocks the drive.

## Changes the drive: setup

For a complete step-by-step setup, including the boot unlock screen (PBA) and
what to do if something goes wrong, see
[FDE-SETUP-GUIDE.md](FDE-SETUP-GUIDE.md). The commands below are a summary.

**Set up a new data drive** (no boot screen on this drive):

```bash
sudo sedutil-cli --initialSetup - /dev/nvme1           # new password, twice
sudo sedutil-cli --setMBREnable off - /dev/nvme1       # data drive: no boot screen
sudo sedutil-cli --enableLockingRange 0 - /dev/nvme1   # turns locking on
```

After `--initialSetup`, the drive has a password but doesn't lock yet.
`--enableLockingRange 0` turns locking on. From then on, the drive locks every
time it loses power.

A data drive set up like this stays locked after a restart until you unlock it
(see above), unless the boot screen on your system drive unlocks it too. It does
that when both drives use the same password.

**Set up the boot drive** (the one with the boot unlock screen): the same
`--initialSetup` and `--enableLockingRange 0` steps, but leave MBR shadowing on and
load the PBA image instead. On the rescue USB the image is in `/usr/sedutil`:

```bash
gunzip /usr/sedutil/UEFI64-*.img.gz
sedutil-cli --loadPBAimage - /usr/sedutil/UEFI64-*.img /dev/nvme0
```

Test the PBA image from a USB stick before you load it onto a drive.

## Changes the drive: turn encryption off or erase

**Turn off locking and keep the data** (back up first):

```bash
sudo sedutil-cli --disableLockingRange 0 - /dev/nvme0
sudo sedutil-cli --setMBREnable off - /dev/nvme0
sudo sedutil-cli --revertNoErase - /dev/nvme0          # Admin1 password
sudo sedutil-cli --query /dev/nvme0                    # must show LockingEnabled = N; if not, stop here
sudo sedutil-cli --revertTPer - /dev/nvme0             # SID password
```

Do them in this order. **Warning:** `--revertTPer` on its own, without `--revertNoErase`
first, **erases the drive**.

**Forgotten password: erase the drive with its PSID.** The PSID is printed on
the drive's label (32 characters; type it in capitals, without dashes). This
destroys all data on the drive, and it's the only way back:

```bash
sudo sedutil-cli --yesIreallywanttoERASEALLmydatausingthePSID <PSID> /dev/nvme0
```

## Error messages

| Message | Usually means |
|---|---|
| `NOT_AUTHORIZED` | Wrong password |
| `AUTHORITY_LOCKED_OUT` | Too many wrong passwords (try limit reached). Power the drive off and on |
| `SP_BUSY` | Another program is using the drive's security features. Close it. If that doesn't help, power the PC off fully |
| `INVALID_PARAMETER` | A step was skipped, usually `--initialSetup` before the other setup commands |
| `Invalid or unsupported disk` | Wrong device name, or the drive isn't an Opal drive |
| `The Kernel flag libata.allow_tpm is not set correctly` | `libata.allow_tpm=1` is missing from the kernel command line (SATA drives only) |

## Good habits

- Shut down or hibernate instead of suspending. A suspended PC keeps its drives unlocked.
- Use a passphrase of five or more random words.
- Write the PSID of every drive down somewhere safe, apart from the PC.
