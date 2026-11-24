# PcInstaller

Installer for LineageOS for PCs (`lineage_pc_x86_64`, device tree `device/pc/pc_x86_64`). It runs inside the
live ISO session and installs LineageOS into a folder on a partition the computer already has; nothing is
repartitioned.

| Part | Where | What |
|---|---|---|
| `PcInstaller` | `src/`, `res/` | Compose (Material 3) app, privileged, platform certificate, `android.uid.system` |
| `PcInstallerCore` | `core/` | the app's logic in plain Kotlin: protocol client, install rules, screen state machine |
| `pc-installd` | `installd/` | root service in C: disks, mounts, copies, GRUB on the ESP, UEFI boot entry |

The service lives here, not in the device tree, because it and the app share one protocol and change together;
the device tree only adds both to `PRODUCT_PACKAGES`.

## What an install produces

```
<partition>/<folder>/            folder: default lineage (kernel argument pc.src=)
    kernel, initrd.img           from the live ISO
    system.img  or  system/      the image, or its files extracted (Linux file systems only)
    data.img    or  userdata/    sparse ext4 image of the chosen size, or a folder (Linux file systems only)
    install-<id>.id              random id; GRUB and pc-init (pc.id=<id>) find the folder by it
    system.old.img / system.old/ the previous system, after an update
<ESP>/EFI/LineageOS/grubx64.efi  GRUB built by build_iso.py (prefix /EFI/LineageOS)
<ESP>/EFI/LineageOS/grub.cfg     menu: normal, safe graphics (nomodeset), previous system (pc.previous=1)
<ESP>/EFI/BOOT/BOOTX64.EFI       the same GRUB, only when the file did not exist
Boot#### + BootOrder             UEFI entry "LineageOS" first in the boot order (reused on reinstall)
```

File system rules: ext4, F2FS and Btrfs take every form; NTFS (`ntfs3`), exFAT and FAT32 take only
`system.img` + `data.img` (FAT32 also caps each file below 4 GiB). BitLocker, LUKS, LVM, swap, ISO 9660 and
unknown partitions, and every partition of the live USB stick, are refused.

An update (the folder already holds a system) removes `system.old*`, renames the system to `system.old*`,
writes the new one and puts the old one back if that fails. The data is always kept.

The kernel command line is the live ISO's `kernel_cmdline()` (`build_iso.py`) plus `pc.id=<id>`:
`androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware console=tty0
loglevel=4 pc.src=<folder> pc.id=<id> quiet`.

## How pc-installd finds the live system

pc-init mounts the ISO's partition at `/mnt/src`, loop-mounts `<folder>/system.img` as Android's root and
switches root, which hides `/mnt/src`. pc-installd reads `pc.live=1` and `pc.src=` from `/proc/cmdline`, takes
the device of `/` from `/proc/self/mountinfo` (a loop device), asks the loop device for its backing device
(`LOOP_GET_STATUS64`) and mounts that partition again, read-only, at `/mnt/pc-installd/src`. The ISO must hold
`<folder>/{kernel,initrd.img,system.img,grubx64.efi}`.

Android does not mount efivarfs; pc-installd mounts it on `/sys/firmware/efi/efivars` before writing the boot
entry. A partition that cannot be mounted read-only to measure its free space (Windows hibernated or with Fast
Startup) is listed with `reason=mount_failed`.

## Protocol

Unix stream socket `/dev/socket/pc_installd` (0660 root:system); only uid 0 and 1000 are served, one client at
a time. The app starts the service with `ctl.start pc_installd`.

Every line is `WORD key=value key=value...\n`. Values are percent-encoded UTF-8: every byte outside
`[A-Za-z0-9._/:+,-]` is written as `%XX`, so values hold no spaces, `=` or newlines. A request gets zero or more
data lines, then `OK` or `ERR code=<error>`.

| Request | Data lines | Meaning |
|---|---|---|
| `INFO` | `INFO version=1 uefi=0/1 live=0/1 source= folder= kernel= initrd= system=` | the live source (sizes in bytes) |
| `LIST` | `PART name= disk= fs= label= size= free= esp=0/1 source=0/1 usable=0/1 reason= folders=0/1 max_file=` | one per partition; `reason` is an error code when `usable=0` |
| `INSPECT part= folder=` | `STATE system=0/1 data=0/1 old=<bytes>` | what the folder already holds |
| `CHECK part= folder= system=image/folder data=image/folder/keep data_size=` | `PLAN mode=install/update need= free= esp=` | validates without writing |
| `INSTALL` (same fields as `CHECK`) | `PROGRESS step=prepare/kernel/system/data/boot/finish percent=0..100` | validates again, then installs |

### Errors

`bad_request`, `bad_folder`, `unknown_partition`, `source_missing`, `source_partition`, `not_uefi`, `no_esp`,
`fs_unsupported`, `bitlocker`, `encrypted`, `fs_images_only`, `file_too_large`, `no_space`, `data_too_small`,
`data_exists`, `no_data_to_keep`, `mount_failed`, `copy_failed`, `data_failed`, `esp_failed`, `efi_failed`,
`io_error`. The app adds `daemon_unreachable`. Each has a one or two sentence text in `res/values*/strings.xml`.

## Tests

```sh
make -C installd test          # 72 C checks (fake sysfs/dev/efivars machine) + socket and cmdline tests (Python)
core/run_tests.sh              # 30 JUnit tests of the app core, with external/kotlinc and JUnit from prebuilts
python3 -m unittest discover -s tests   # UI strings: both languages, placeholders, no em dash
tools/typecheck.sh             # compiles core + Compose UI against the prebuilts (needs a previous build's out/)
```

The C tests use `mke2fs`, `mkfs.vfat`, `sgdisk` and `sfdisk` from the host when present and skip otherwise.
In Soong: `PcInstallerCoreTests` (`java_test_host`, `atest`-able).

## Build integration

- local manifest: `<project path="packages/apps/PcInstaller" name="android_packages_apps_PcInstaller" remote="pc" revision="main" />`
- `device.mk`: `PRODUCT_PACKAGES += PcInstaller pc-installd`
- `BoardConfig.mk`: `SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += packages/apps/PcInstaller/installd/sepolicy/private`
- device tree branch `installer`: `pc.id=` in pc-init, `grubx64.efi` and `install-live.id` on the ISO,
  `pc.id=live` on the live entries, raw `system.img` (`TARGET_USERIMAGES_SPARSE_EXT_DISABLED`)

## Not done yet

- BIOS/legacy boot: the installer refuses it (`not_uefi`); it needs GRUB's i386-pc core image in the MBR or
  a BIOS boot partition.
- Secure Boot: GRUB is unsigned; firmware with Secure Boot on will not start it (shim + signed GRUB).
- SELinux: `pc_installd` is a permissive domain. Enforcing needs relabel rights for every file type
  (extracting the system writes `security.selinux`) and an efivarfs label.
- A GRUB entry for Windows or other systems (chainload `bootmgfw.efi`); today they are reached through the
  firmware's boot menu.
- Several installs on one machine share `EFI/LineageOS/grub.cfg`; the last one wins.
- The installer shows on an installed system too; it then says to start from the USB drive (`source_missing`).
- Partitions inside LVM/RAID and file systems on whole disks without a partition table are not listed.
