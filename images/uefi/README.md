# UEFI PBA and Rescue images

This directory builds the sedutil pre-boot authorization (PBA) image and a
Rescue USB image for 64-bit UEFI PCs. GitHub Actions builds both on every
change (`.github/workflows/pba.yml`), boot-tests them in QEMU, and attaches
them to a GitHub Release when a `v*` tag is pushed.

| Image | Use |
| --- | --- |
| `sedutil-pba-uefi64.img.gz` | The PBA. Unzip it and load it into every drive's shadow MBR with `sedutil-cli --loadPBAimage`. It asks for the password once, unlocks every Opal drive it finds, then reboots into your normal boot loader. |
| `sedutil-rescue-uefi64.img.gz` | Write it to a USB stick (Rufus in DD mode, or `dd`). Boots to a root shell with `sedutil-cli`, to unlock drives by hand, turn a PBA off, or load another PBA. |
| `BOOTX64-*.EFI` | The kernel of each image on its own (initramfs and command line inside), for checking or signing. |

## What is inside

- Buildroot 2026.02.3 (long-term-support branch) with the Bootlin glibc toolchain.
- Linux 6.18 longterm, `x86_64_defconfig` plus `external/board/common/linux.fragment`.
  Everything is built in: NVMe, AHCI SATA, USB host controllers, USB storage,
  USB and PS/2 keyboards, hidraw, the UEFI framebuffer console.
- No boot loader. UEFI starts the kernel directly through its EFI stub from
  `/EFI/BOOT/BOOTX64.EFI`. The kernel command line is fixed at build time and
  firmware or boot-menu arguments are ignored.
- The PBA has no login and no shell prompt. The root account is locked, init
  starts `linuxpba` on the first console, and every way out of it reboots.
  Ctrl+C, Ctrl+\ and Ctrl+Z are ignored, SysRq is off, and a kernel panic
  reboots after 5 seconds. Kernel lockdown (integrity mode) is on.
- The PBA waits up to 10 seconds for drives to appear, so slow NVMe and USB
  drives are not missed, and skips a device that does not answer instead of
  stopping.
- Keyboard layout: US.

## Building by hand

On a Linux machine with the usual Buildroot host tools:

    images/uefi/build.sh pba    ~/sedutil-build
    images/uefi/build.sh rescue ~/sedutil-build

The work directory must be outside the repository. A first build takes
30 to 60 minutes. The QEMU test needs `qemu-system-x86` and `ovmf`:

    python3 images/uefi/test/qemu_boot_test.py pba ~/sedutil-build/pba/images/sedutil-pba-uefi64.img

The older BIOS, 32-bit and syslinux scripts in `images/` are kept for
reference and are no longer built.
