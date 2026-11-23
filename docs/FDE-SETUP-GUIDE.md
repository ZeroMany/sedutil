# Setting up Opal full-disk encryption with sedutil

This guide turns on hardware encryption for Opal 2 self-encrypting drives (SEDs)
and installs the boot unlock screen (the PBA, pre-boot authorization) that asks
for your password every time the PC is switched on.

It follows the original
[Drive Trust Alliance instructions](https://github.com/Drive-Trust-Alliance/sedutil/wiki/Encrypting-your-drive),
adapted for NVMe drives and AMD Ryzen systems with the 64-bit UEFI images from
this fork (or ChubbyAnt's, which it's based on). For everyday commands once
you're set up, see [QUICK-REFERENCE-WINDOWS.md](QUICK-REFERENCE-WINDOWS.md) (Windows) and
[QUICK-REFERENCE-LINUX.md](QUICK-REFERENCE-LINUX.md).

> **Back up all your data before you start.** A wrong command or a drive with
> buggy firmware can make the data unreadable.

## Contents

1. [Before you start](#1-before-you-start)
2. [Boot the rescue USB and check your drives](#2-boot-the-rescue-usb-and-check-your-drives)
3. [Test the PBA](#3-test-the-pba)
4. [Set up the boot drive](#4-set-up-the-boot-drive)
5. [Set up your other drives](#5-set-up-your-other-drives)
6. [Power off and try it](#6-power-off-and-try-it)
7. [Living with encrypted drives](#7-living-with-encrypted-drives)
8. [If something goes wrong](#8-if-something-goes-wrong)
9. [Turn encryption off](#9-turn-encryption-off)

## 1. Before you start

**You need:**

- A USB stick you can wipe.
- The rescue image (`Rescue64-*.img.gz`) from the release page. It contains
  `sedutil-cli` and the PBA image.
- Rufus (Windows) or `dd` (Linux) to write the image to the USB stick. Ventoy
  also works.

**Do these first:**

- **Write down each drive's PSID.** It's a 32-character code printed on the
  drive's label. Take a photo of each label and keep it somewhere other than
  the PC. If you ever forget your password, the PSID is the only way to make the
  drive usable again, and that erases it.
- **Update each drive's firmware** with the manufacturer's tool. Firmware
  updates sometimes fix encryption bugs, and some can't be installed once the
  drive is locked.
- **Turn off any other drive encryption** on these drives: BitLocker using
  hardware encryption, or the encryption feature in Samsung Magician or a
  similar tool. Two programs can't own the same drive.
- **Choose your password.** A passphrase of five to seven random words is strong
  and easy to type. The boot screen always uses a **US keyboard layout**. If
  your keyboard uses another layout, avoid characters that are in a different
  place on a US keyboard (most symbols, and letters like Y/Z on a German
  keyboard), or you won't be able to type your password at boot.

**Good to know:**

- The rescue system runs in memory and forgets everything at power-off, so it's
  safe to type your password straight into commands there. On your normal
  Windows or Linux system, type `-` in place of the password instead, so it
  stays out of your command history.
- Type the commands exactly as shown. Every option starts with two plain
  hyphens (`--`). Word processors often turn `--` into a dash (`–` or `—`),
  which sedutil doesn't recognize, so don't copy commands from a Word file.
- The examples use `/dev/nvme0` for the boot drive and `/dev/nvme1` for a
  second drive. SATA drives appear as `/dev/sda`, `/dev/sdb` and so on. Use the
  names `--scan` shows for your drives.

## 2. Boot the rescue USB and check your drives

1. Write the rescue image to the USB stick. In Rufus, pick the `.img.gz` file
   and accept DD image mode if it asks.
2. Restart, enter the BIOS/UEFI setup, and **turn off Secure Boot**. The PBA
   isn't signed for Secure Boot, so it can't start with it on. You don't need
   CSM or legacy boot.
3. Boot from the USB stick.
4. At the login prompt, type `root`. There is no password.
5. Run:

   ```
   sedutil-cli --scan
   ```

   Example output:

   ```
   Scanning for Opal compliant disks
   /dev/nvme0  2  Samsung SSD 960 EVO 250GB                2B7QCXE7
   /dev/sda    2  Crucial_CT250MX200SSD1                   MU04
   /dev/sdb   12  Samsung SSD 850 EVO 500GB                EMT01B6Q
   No more disks present ending scan
   ```

6. Check that every drive you want to encrypt has a **2** in the second column
   (`2` or `12`), which means Opal 2. **If a drive doesn't, stop: don't run any
   of the commands below on it.**

## 3. Test the PBA

Before you change anything, check that the PBA can see your drives on this PC.

```
linuxpba
```

Type `debug` as the passphrase. Each drive should be listed as `is OPAL NOT LOCKED`.
A drive listed as `NOT OPAL` can't be unlocked by the PBA, so don't encrypt it.

| PBA message | Meaning |
|---|---|
| `is OPAL NOT LOCKED` | The drive works with the PBA, and isn't locked (or isn't set up yet) |
| `is OPAL Unlocked` | The PBA unlocked the drive |
| `is OPAL Failed` | The unlock failed: usually a wrong password |
| `NOT OPAL` | The PBA can't manage this drive |

> **Note for builds from this fork:** for security, this fork's PBA no longer
> treats `debug` as special. It always restarts the PC after trying the
> password, so the list is only on screen briefly. Watch for it, or skip this
> test and rely on the test in step 6. ChubbyAnt's images still accept `debug`.

## 4. Set up the boot drive

The boot drive is the one Windows starts from. It holds the PBA.

```
sedutil-cli --initialsetup <password> /dev/nvme0
sedutil-cli --enablelockingrange 0 <password> /dev/nvme0
sedutil-cli --setlockingrange 0 lk <password> /dev/nvme0
sedutil-cli --setmbrdone off <password> /dev/nvme0
gunzip /usr/sedutil/UEFI64-*.img.gz
sedutil-cli --loadpbaimage <password> /usr/sedutil/UEFI64-*.img /dev/nvme0
```

What each command does:

| Command | What it does |
|---|---|
| `--initialsetup` | Takes ownership of the drive and sets your password. It sets both the SID (owner) and Admin1 (unlock) passwords to the same value, and turns on the hidden boot area that will hold the PBA. The drive doesn't lock yet |
| `--enablelockingrange 0` | Turns on locking for the whole drive. From now on, it locks every time it loses power |
| `--setlockingrange 0 lk` | Locks the drive right now. Optional, since powering off locks it anyway |
| `--setmbrdone off` | Makes the drive show the PBA instead of Windows. This also happens at every power-on |
| `--loadpbaimage` | Writes the PBA into the drive's hidden boot area. It takes a few minutes |

Example output:

```
#sedutil-cli --initialsetup <password> /dev/nvme0
- 14:06:39.709 INFO: takeOwnership complete
- 14:06:41.703 INFO: Locking SP Activate Complete
- 14:06:42.317 INFO: LockingRange0 disabled
- 14:06:42.694 INFO: LockingRange0 set to RW
- 14:06:43.515 INFO: MBRDone set on
- 14:06:43.904 INFO: MBREnable set on
- 14:06:43.904 INFO: Initial setup of TPer complete on /dev/nvme0
#sedutil-cli --enablelockingrange 0 <password> /dev/nvme0
- 14:07:24.914 INFO: LockingRange0 enabled ReadLocking,WriteLocking
#sedutil-cli --setlockingrange 0 lk <password> /dev/nvme0
- 14:07:46.728 INFO: LockingRange0 set to LK
#sedutil-cli --setmbrdone off <password> /dev/nvme0
- 14:08:21.999 INFO: MBRDone set off
#sedutil-cli --loadpbaimage <password> /usr/sedutil/UEFI64-*.img /dev/nvme0
- 14:10:55.328 INFO: Writing PBA to /dev/nvme0
33554432 of 33554432 100% blk=1500
- 14:14:04.499 INFO: PBA image /usr/sedutil/UEFI64-*.img written to /dev/nvme0
```

**Check that you typed the password the way you meant to.** Run this. If it
prints `MBRDone set on`, the password is right:

```
sedutil-cli --setmbrdone on <password> /dev/nvme0
```

If it fails with `NOT_AUTHORIZED`, the password you set isn't the one you think.
Don't power off. Go to [Turn off locking and the PBA](#turn-off-locking-and-the-pba)
and use the password exactly as you typed it in `--initialsetup`.

**If `--initialsetup` fails** with `NOT_AUTHORIZED` or `NOT_VALID`, the drive
already has an owner: it was set up before, by sedutil or by another program
such as BitLocker or Samsung Magician. Your options:

- If you know the old password, use it to remove that setup first (see
  [Turn encryption off](#9-turn-encryption-off)), then start this step again.
- If you don't know the old password, the only way forward is a PSID revert,
  which **erases everything on the drive**. See [Forgotten password](#forgotten-password-psid-erase).
  Back up first if the drive is still readable.

## 5. Set up your other drives

For every other drive you want to encrypt, run the same commands as the boot
drive, PBA included. The PBA image is already unpacked from step 4:

```
sedutil-cli --initialsetup <password> /dev/nvme1
sedutil-cli --enablelockingrange 0 <password> /dev/nvme1
sedutil-cli --setlockingrange 0 lk <password> /dev/nvme1
sedutil-cli --setmbrdone off <password> /dev/nvme1
sedutil-cli --loadpbaimage <password> /usr/sedutil/UEFI64-*.img /dev/nvme1
```

**Use the same password as the boot drive.** At power-on, the PBA tries the
password you type on every drive, so drives with the same password all unlock
together. A drive with a different password stays locked until you unlock it
separately.

**Why put the PBA on every drive:** any drive can then unlock itself, and all
the others, on its own. If the boot drive fails or is removed, the PC still
shows the PBA from another drive and unlocks the rest. A drive moved to
another PC can be unlocked at that PC's boot, as long as it boots from the
drive with Secure Boot off. The PBA lives in a separate hidden area, so it
uses none of the drive's space. It holds no secrets, and it can only be
changed with your password.

Things to know:

- Only one PBA runs at each boot: the one on the first drive in the boot
  order. It unlocks every drive, so boot time is the same as with one PBA.
- Your BIOS boot menu may list a boot entry for each drive. Any of them works.
- When the PBA is updated, load the new image onto **every** drive.
- When you unlock one of these drives from Windows instead of at boot, also
  run `--setmbrdone on` so Windows sees its contents (the quick reference
  shows how).

**If you'd rather keep the PBA on the boot drive only**, leave out the last two
commands and turn the hidden boot area off instead:

```
sedutil-cli --setmbrenable off <password> /dev/nvme1
```

**Samsung 850 EVO (and other older SATA drives): give it a new key.** Research
in 2018 found that these drives can keep an old, unprotected copy of their
encryption key in flash after a password is set. Making a new key after setup
fixes that, but it **erases the drive**, so do it only on an empty drive, or
back up first and restore afterwards:

```
sedutil-cli --rekeylockingrange 0 <password> /dev/sda
```

## 6. Power off and try it

1. Power off completely. Don't restart:

   ```
   poweroff
   ```

   A restart keeps power to the drives, so they stay unlocked. Only a full power-off
   locks them.
2. Remove the USB stick and switch the PC on. The PBA should ask for your
   password.
3. Type it. The PBA unlocks the drives and restarts the PC, and then Windows
   starts as usual.

If the PC goes straight to an error or a black screen without asking for the
password, check in the BIOS that the boot drive is first in the boot order and
that Secure Boot is still off.

## 7. Living with encrypted drives

- **Always shut down or hibernate. Don't use sleep.** A sleeping PC can keep the
  drives unlocked, and on some PCs the drives lose power during sleep and lock,
  so Windows can't wake up. Turn sleep off in Windows: Settings, System, Power.
- **Change your password** on every drive, keeping SID and Admin1 the same:

  ```
  sedutil-cli --setsidpassword <old> <new> /dev/nvme0
  sedutil-cli --setadmin1pwd <old> <new> /dev/nvme0
  ```

- **Limit wrong guesses** with `--setTryLimit` (in builds from this fork). See
  the quick reference.
- **Lock or unlock a data drive from Windows** without restarting: see the
  quick reference.

## 8. If something goes wrong

**The PBA doesn't unlock a drive** (`is OPAL Failed`): the password is wrong,
or this drive has a different password. Power off fully and try again. After
too many wrong tries, a drive with a try limit refuses all attempts until it's
powered off.

**You can't get past the PBA at all:** boot the rescue USB (step 2), then for
each drive:

```
sedutil-cli --setlockingrange 0 rw <password> /dev/nvme0
sedutil-cli --setmbrdone on <password> /dev/nvme0
```

Restart (don't power off). The PC boots Windows with the drives unlocked. From
there you can turn off locking (below) and work out what went wrong.

### Forgotten password: PSID erase

If nobody knows the password, the drive can only be reset with its PSID. **This
erases everything on the drive. There's no way to keep the data.**

```
sedutil-cli --query /dev/nvme0
sedutil-cli --yesIreallywanttoERASEALLmydatausingthePSID <PSID> /dev/nvme0
```

Type the PSID in capitals, without dashes. You should see
`revertTper completed successfully`. `--PSIDrevert` is another name for the same
command, and it erases the drive too. If you get `NOT_AUTHORIZED`, the PSID was
mistyped.

After a PSID erase, the drive is back to factory state and you can set it up
again from step 4.

## 9. Turn encryption off

### Turn off locking and the PBA

This keeps Opal set up, so you can turn locking back on later. Run it on each
drive:

```
sedutil-cli --disablelockingrange 0 <password> /dev/nvme0
sedutil-cli --setmbrenable off <password> /dev/nvme0
```

Example output:

```
- 14:07:24.914 INFO: LockingRange0 disabled
- 14:08:21.999 INFO: MBREnable set off
```

To turn it back on:

```
sedutil-cli --enablelockingrange 0 <password> /dev/nvme0
sedutil-cli --setmbrenable on <password> /dev/nvme0
```

### Remove Opal and keep the data

This returns the drive to its factory state without erasing it. **Back up
first:** some drives have firmware bugs that erase the data anyway. The original
sedutil project only lists a few drive models as tested.

1. Remove the locking setup (Admin1 password):

   ```
   sedutil-cli --revertnoerase <password> /dev/nvme0
   ```

   You should see `Revert LockingSP complete`.

2. Check that it worked:

   ```
   sedutil-cli --query /dev/nvme0
   ```

   In the `Locking function` section, look for `LockingEnabled = N`. **If it
   doesn't say `LockingEnabled = N`, stop here. The next command would erase all
   your data.**

3. Remove Opal ownership (SID password):

   ```
   sedutil-cli --reverttper <password> /dev/nvme0
   ```

   You should see `revertTper completed successfully`.

The drive is now back to its factory state, with your data intact. You can set
it up again whenever you like.

**When you're done with the rescue USB, always finish with `poweroff`**, so
every change takes effect and the drives lock.
