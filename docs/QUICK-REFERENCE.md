# sedutil quick reference (Windows)

Everyday commands for Opal self-encrypting drives, with examples. For Linux, see
[QUICK-REFERENCE-LINUX.md](QUICK-REFERENCE-LINUX.md).

## Before you start

- Run everything from **PowerShell as Administrator**.
- Drive names look like `\\.\PhysicalDrive2`. The number is the same as the disk
  number in Disk Management and `Get-Disk`.
- **Type `-` in place of any password** and sedutil asks for it at a hidden prompt.
  A password typed straight into the command line ends up in PowerShell's history
  file in plain text. When you set a new password, the prompt asks for it twice.

  > **If you typed a password into the command line by mistake**, remove it from
  > PowerShell's history. Either open the history file and delete that line:
  >
  > ```powershell
  > notepad (Get-PSReadLineOption).HistorySavePath
  > ```
  >
  > or delete the whole history file:
  >
  > ```powershell
  > Remove-Item (Get-PSReadLineOption).HistorySavePath
  > ```
  >
  > Then close every PowerShell window, so the up-arrow keys can't bring the
  > password back either.

- **Warning: Never lock, erase or revert the drive Windows is running from.**
- Don't use `-n`. It turns off password hashing, and a drive set up without
  hashing can't be unlocked by the boot screen (PBA).
- Back up before anything in the "Changes the drive" sections.

## Look, don't touch (read-only)

| What | Command |
|---|---|
| List drives and Opal support | `sedutil-cli --scan` |
| Show one drive's state | `sedutil-cli --query \\.\PhysicalDrive2` |
| Is this drive an SED? | `sedutil-cli --isValidSED \\.\PhysicalDrive2` |
| Match numbers to drive models and serial numbers | `Get-Disk \| Format-Table Number, FriendlyName, SerialNumber, Size` |
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
| `MBRDone = Y` | The PBA has been passed since power-on, so Windows sees the real contents |

Opal drives re-lock themselves whenever they lose power, so a powered-off
drive is always locked. `Locked = N` on a running PC just means it was unlocked
since power-on, usually by the boot screen.

## Lock and unlock a data drive

The drive locks with your Admin1 password, which is the password you chose at setup.
Replace `2` with your disk number.

**Lock**

```powershell
Set-Disk -Number 2 -IsOffline $true                      # Windows finishes writing and lets go of the disk
sedutil-cli --setLockingRange 0 LK - \\.\PhysicalDrive2  # prompts for the password
sedutil-cli --query \\.\PhysicalDrive2                   # Optional to verify the drive is locked; should show Locked = Y
```

**Unlock**

```powershell
sedutil-cli --setLockingRange 0 RW - \\.\PhysicalDrive2
sedutil-cli --setMBRDone on - \\.\PhysicalDrive2          # only if MBREnabled = Y and MBRDone = N
Set-Disk -Number 2 -IsOffline $false                      # its drive letters come back
```

Notes:

- **Always take the disk offline before you lock it.** If you lock a disk Windows is
  still using, data Windows hasn't written yet can be lost, and the file system
  can be damaged.
- Windows remembers that you took a disk offline, even across restarts. If a
  drive letter goes missing after a restart, run `Set-Disk -Number 2 -IsOffline $false`.
- `--setLockingRange 0 RO` makes the drive read-only instead of locked.

## Changes the drive: passwords and limits

**Change the password.** A drive has two passwords: SID (owner) and Admin1
(locking). `--initialSetup` makes them the same, and the boot screen unlocks
every drive with the one password you type, so keep both passwords the same on all drives.

```powershell
sedutil-cli --setSIDPassword - - \\.\PhysicalDrive2       # current, then new twice
sedutil-cli --setPassword - Admin1 - \\.\PhysicalDrive2   # current, then new twice
```

**Limit wrong guesses.** By default a drive allows unlimited password attempts.
This command makes it refuse further attempts until it's power-cycled, after the
given number of wrong passwords. It's slower for an attacker, and harmless for you:
switch the PC off and on and try again. Some drives refuse this setting, and
sedutil says so if yours does.

```powershell
sedutil-cli --setTryLimit 5 - \\.\PhysicalDrive2          # 0 removes the limit
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

```powershell
sedutil-cli --initialSetup - \\.\PhysicalDrive2           # new password, twice
sedutil-cli --setMBREnable off - \\.\PhysicalDrive2       # data drive: no boot screen
sedutil-cli --enableLockingRange 0 - \\.\PhysicalDrive2   # turns locking on
```

After `--initialSetup`, the drive has a password but doesn't lock yet.
`--enableLockingRange 0` turns locking on. From then on, the drive locks every
time it loses power.

A data drive set up like this stays locked after a restart until you unlock it
from Windows (see above), unless the boot screen on your Windows drive unlocks it
too. It does that when both drives use the same password.

**Set up the boot drive** (the one with the boot unlock screen): the same
`--initialSetup` and `--enableLockingRange 0` steps, but leave MBR shadowing on and
load the PBA image instead:

```powershell
sedutil-cli --loadPBAimage - C:\path\UEFI64.img \\.\PhysicalDrive0
```

Test the PBA image from a USB stick before you load it onto a drive.

## Changes the drive: turn encryption off or erase

**Turn off locking and keep the data** (back up first):

```powershell
sedutil-cli --disableLockingRange 0 - \\.\PhysicalDrive2
sedutil-cli --setMBREnable off - \\.\PhysicalDrive2
sedutil-cli --revertNoErase - \\.\PhysicalDrive2         # Admin1 password
sedutil-cli --query \\.\PhysicalDrive2                   # must show LockingEnabled = N; if not, stop here
sedutil-cli --revertTPer - \\.\PhysicalDrive2            # SID password
```

Do them in this order. **Warning:** `--revertTPer` on its own, without `--revertNoErase`
first, **erases the drive**.

**Forgotten password: erase the drive with its PSID.** The PSID is printed on
the drive's label (32 characters; type it in capitals, without dashes). This destroys all data on the drive, and it's
the only way back:

```powershell
sedutil-cli --yesIreallywanttoERASEALLmydatausingthePSID <PSID> \\.\PhysicalDrive2
```

## Error messages

| Message | Usually means |
|---|---|
| `NOT_AUTHORIZED` | Wrong password |
| `AUTHORITY_LOCKED_OUT` | Too many wrong passwords (try limit reached). Power the drive off and on |
| `SP_BUSY` | Another program is using the drive's security features. Close it, or restart |
| `Invalid or unsupported disk` | Wrong drive number, or the drive isn't an Opal drive |

## Good habits

- Shut down or hibernate instead of sleeping. A sleeping PC keeps its drives unlocked.
- Use a passphrase of five or more random words.
- Write the PSID of every drive down somewhere safe, apart from the PC.
