# PcInstaller

Installer for LineageOS for PCs (`lineage_pc_x86_64`, device tree `device/pc/pc_x86_64`). It runs inside the
live ISO session and installs LineageOS into a folder on a partition the computer already has; nothing is
repartitioned.

| Part | Where | What |
|---|---|---|
| `PcInstaller` | `src/`, `res/` | Compose (Material 3) app, privileged, platform certificate, `android.uid.system` |
| `PcInstallerCore` | `core/` | the app's logic in plain Kotlin: protocol client, install rules, screen state machine |
| `pc-installd` | `installd/` | root service in C: disks, mounts, copies, GRUB for UEFI and BIOS, boot entries, menus |

The service lives here, not in the device tree, because it and the app share one protocol and change together;
the device tree only adds both to `PRODUCT_PACKAGES`.

## What the app does

- **Live USB session:** install into a folder on a partition (UEFI or BIOS), update an install that is
  already in that folder (data kept), or remove it.
- **Installed system:** "Update from a USB drive": plug in a newer live USB drive, the app finds it, shows
  the installed and the new version, and updates the running install in place (data kept).

## What an install produces

```
<partition>/<folder>/            folder: default lineage (kernel argument pc.src=)
    kernel, initrd.img           from the live ISO
    system.img  or  system/      the image, or its files extracted (Linux file systems only)
    data.img    or  userdata/    sparse ext4 image of the chosen size, or a folder (Linux file systems only)
    install-<id>.id              random id, kept by updates; GRUB and pc-init (pc.id=<id>) find the folder by it
    system.old.img / system.old/ the previous system, after an update

UEFI
<ESP>/EFI/LineageOS/grubx64.efi  GRUB built by build_iso.py (prefix /EFI/LineageOS)
<ESP>/EFI/BOOT/BOOTX64.EFI       the same GRUB, only when the file did not exist
Boot#### + BootOrder             UEFI entry "LineageOS" first in the boot order (reused on reinstall)

BIOS
sector 0 of the target disk      GRUB's boot.img; partition table and disk id kept, old boot code saved
core.img                         in the GPT BIOS boot partition, else the free sectors before the first
                                 partition (from LBA 34 on GPT, LBA 1 on MBR)
<partition>/pc-grub/i386-pc/     GRUB's modules; core.img finds /pc-grub/grub.cfg and loads them from here
<partition>/pc-grub/mbr-<disk>.bak  the boot code GRUB replaced

Every menu directory (EFI/LineageOS, pc-grub)
    entries/<id>.cfg             one per install: its entry plus a submenu with safe graphics (nomodeset)
                                 and the previous system (pc.previous=1)
    grub.cfg                     rebuilt from all entries + the other systems found + (UEFI) firmware settings
```

**One menu for every install.** An install adds its `entries/<id>.cfg` to every menu directory on the
computer and rebuilds each `grub.cfg`, so whichever GRUB starts lists every install; an update replaces only
its own entry; a removal deletes it, and the last removal takes GRUB with it (EFI directory, our fallback
loader, the UEFI entry, or the BIOS boot code put back from the backup).

**Other systems.** UEFI menus chainload Windows Boot Manager (`EFI/Microsoft/Boot/bootmgfw.efi`) and other
Linux loaders (`EFI/<distro>/shimx64.efi`, `grubx64.efi`, `EFI/systemd/systemd-bootx64.efi`) found on any ESP.
BIOS menus chainload the boot sector of partitions holding `bootmgr`/`ntldr` (Windows) and open other
Linux GRUB menus (`boot/grub2/grub.cfg`, `boot/grub/grub.cfg`, ...) with `configfile`. Each is found by its
file system UUID, which pc-installd reads from the superblock the way GRUB prints it.

File system rules: ext4, F2FS and Btrfs take every form; NTFS (`ntfs3`), exFAT and FAT32 take only
`system.img` + `data.img` (FAT32 also caps each file below 4 GiB). Disks formatted without a partition table
are listed like partitions. BitLocker, LUKS, LVM, RAID members, swap, ISO 9660 and unknown partitions, and
every partition of the live USB stick, are refused with a one-sentence reason.

BIOS needs room for core.img (255 sectors): disks partitioned in the last 15 years leave 1 MiB before the first
partition. Only a disk whose first partition starts at sector 63 (or a disk with 4 KiB sectors, which BIOS
cannot boot from) is refused (`no_boot_room`).

An update removes `system.old*`, renames the system to `system.old*`, writes the new one and puts the old one
back if that fails. A removal deletes only the files an install writes; other files in the folder stay.

The kernel command line is the live ISO's `kernel_cmdline()` (`build_iso.py`) plus `pc.id=<id>`:
`androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware console=tty0
loglevel=4 pc.src=<folder> pc.id=<id> quiet`.

## How pc-installd finds the systems

**Live session.** pc-init mounts the ISO's partition at `/mnt/src`, loop-mounts `<folder>/system.img` as
Android's root and switches root, which hides `/mnt/src`. pc-installd reads `pc.live=1` and `pc.src=` from
`/proc/cmdline`, takes the device of `/` from `/proc/self/mountinfo` (a loop device), asks the loop device for
its backing device (`LOOP_GET_STATUS64`) and mounts that partition again, read-only, at `/mnt/pc-installd/src`.
The ISO must hold `<folder>/{kernel,initrd.img,system.img,grubx64.efi,install-live.id}` and
`<folder>/grub-pc/{boot.img,core.img,i386-pc/}`.

**Installed system.** `pc.src=` and `pc.id=` come from `/proc/cmdline`; the install's partition is the device
behind `/` (through its loop device for `system.img`). A live USB drive is any other partition holding
`lineage/install-live.id`; its version is `ro.build.date.utc` / `ro.lineage.display.version` from the
`system/build.prop` inside its `system.img`, compared with `/system/build.prop`. The running install can be
updated (the loop device keeps the old image open until the reboot) but not removed.

Android does not mount efivarfs; pc-installd mounts it on `/sys/firmware/efi/efivars` before reading
`SecureBoot` or writing boot entries. A partition that cannot be mounted read-only to measure its free space
(Windows hibernated or with Fast Startup) is listed with `reason=mount_failed`.

## Protocol (version 2)

Unix stream socket `/dev/socket/pc_installd` (0660 root:system); only uid 0 and 1000 are served, one client at
a time. The app starts the service with `ctl.start pc_installd`.

Every line is `WORD key=value key=value...\n`. Values are percent-encoded UTF-8: every byte outside
`[A-Za-z0-9._/:+,-]` is written as `%XX`, so values hold no spaces, `=` or newlines. A request gets zero or more
data lines, then `OK` or `ERR code=<error>`.

| Request | Data lines | Meaning |
|---|---|---|
| `INFO` | `INFO version=2 uefi= secure_boot= live= source= folder= kernel= initrd= system= installed= install_part= install_folder= install_system=image/folder install_version= install_build_utc=` | the live source (sizes in bytes) or the running install |
| `LIST` | `PART name= disk= fs= label= size= free= esp= source= usable= reason= folders= max_file=` | one per partition or whole-disk file system; `reason` is an error code when `usable=0` |
| `INSPECT part= folder=` | `STATE system= data= old=<bytes>` | what the folder already holds |
| `CHECK part= folder= system=image/folder data=image/folder/keep data_size=` | `PLAN mode=install/update need= free= boot=uefi/bios esp=` | validates without writing |
| `INSTALL` (same fields as `CHECK`) | `PROGRESS step=prepare/kernel/system/data/boot/finish percent=0..100` | validates again, then installs or updates |
| `UNINSTALL part= folder=` | `PROGRESS step=prepare/remove/boot/finish percent=` | removes the install and its menu entries |
| `FIND_UPDATE` | `UPDATE source= version= build_utc= current_version= current_utc= newer=0/1` | installed system: finds a live USB drive and makes it the source of the next `INSTALL` |

### Errors

`bad_request`, `bad_folder`, `unknown_partition`, `source_missing`, `source_partition`, `no_esp`,
`no_boot_room`, `fs_unsupported`, `bitlocker`, `encrypted`, `lvm`, `raid`, `fs_images_only`, `file_too_large`,
`no_space`, `data_too_small`, `data_exists`, `no_data_to_keep`, `mount_failed`, `copy_failed`, `data_failed`,
`esp_failed`, `efi_failed`, `boot_failed`, `menu_failed`, `io_error`, `not_installed`, `running_system`,
`no_update_source`. The app adds `daemon_unreachable`. Each has a one or two sentence text in
`res/values*/strings.xml`.

## Tests

```sh
make -C installd test          # 116 C checks (fake sysfs/dev/efivars machine) + socket and cmdline tests (Python)
core/run_tests.sh              # 38 JUnit tests of the app core, with external/kotlinc and JUnit from prebuilts
python3 -m unittest discover -s tests   # UI strings: both languages, placeholders, no em dash
tools/typecheck.sh             # compiles core + Compose UI against the prebuilts (needs a previous build's out/)
tools/bios_boot_test.sh        # boots GRUB for BIOS written by biosboot.c in QEMU/SeaBIOS (three disk layouts)
```

The C tests use `mke2fs`, `mkfs.vfat`, `sgdisk`, `sfdisk` and `grub2-script-check` (every generated menu goes
through GRUB's own parser) from the host when present and skip otherwise. The BIOS boot code was also booted
in QEMU/SeaBIOS from an MBR gap, a GPT gap and a GPT BIOS boot partition: GRUB found `/pc-grub/grub.cfg` each
time. In Soong: `PcInstallerCoreTests` (`java_test_host`, `atest`-able).

## Build integration

- local manifest: `<project path="packages/apps/PcInstaller" name="android_packages_apps_PcInstaller" remote="pc" revision="lineage-24.0" />`
- `device.mk`: `PRODUCT_PACKAGES += PcInstaller pc-installd`
- `BoardConfig.mk`: `SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += packages/apps/PcInstaller/installd/sepolicy/private`
- the device tree's `iso/build_iso.py` puts `grubx64.efi`, `grub-pc/` and `install-live.id` on the ISO

## Limits

- **Secure Boot.** The GRUB we ship is not signed by a key the firmware trusts, and only Microsoft or a
  distribution's shim key could make it so. The installer reads the `SecureBoot` variable and tells the user
  to turn it off before restarting.
- **SELinux.** `pc_installd` is a permissive domain without allow rules: AOSP's neverallows forbid a
  coredomain from mounting file systems, writing raw block devices and relabeling every file type, all of
  which the installer must do. An enforcing build needs a vendor-side helper domain.
- **LVM and RAID.** Android has no LVM or md tools to assemble the volumes, so their members are listed with
  the reason instead of being offered.

## Credits

- [LineageOS](https://lineageos.org) ([GitHub](https://github.com/LineageOS)): the base system and build this is part of.
- [Android Open Source Project](https://source.android.com): the platform, its APIs and build system.
- [BlissOS](https://blissos.org) ([GitHub](https://github.com/BlissOS)) and the [Android-Generic Project](https://github.com/android-generic): the approach to running Android on PCs that this project follows. Its installer, [aaropa](https://github.com/BlissOS/bootable_aaropa), inspired the install layout (no code is shared).
- [Android-x86](https://www.android-x86.org): the original Android-on-PC work, including installing into a folder and booting through GRUB. Its `newinstaller` defined the folder install this follows.
- [GRUB](https://www.gnu.org/software/grub/): the boot loader the installer sets up.

Thanks to everyone behind these projects. Files taken from them keep their original licenses and copyright notices.
