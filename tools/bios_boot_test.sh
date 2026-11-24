#!/bin/bash
# Boots GRUB for BIOS, written by pc-installd's biosboot.c, in QEMU/SeaBIOS from three disk layouts (MBR gap,
# GPT gap, GPT BIOS boot partition) and checks that it reads /pc-grub/grub.cfg. Needs grub2-mkimage (i386-pc),
# sfdisk, sgdisk, mke2fs and qemu-system-x86_64; uses 128 MiB of RAM for a few seconds per layout.
#   [PC_ISO_DIR=device/pc/pc_x86_64/iso] tools/bios_boot_test.sh [WORK_DIR]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
TOP=${ANDROID_BUILD_TOP:-$(cd "$HERE/../../.." && pwd)}
WORK=${1:-$HERE/build/bios-boot}
ISO_DIR=${PC_ISO_DIR:-$TOP/device/pc/pc_x86_64/iso}
rm -rf "$WORK" && mkdir -p "$WORK/grub" "$WORK/build" "$WORK/root/pc-grub" "$WORK/root/lineage"
cd "$WORK"
python3 -I -c "
import sys; sys.path.insert(0, '$ISO_DIR')
from pathlib import Path
import build_iso
build_iso.build_bios_grub(Path('grub/out'), Path('build'))"
cc -Wall -I"$HERE/installd/src" -o write "$HERE/tools/write_bios_boot.c" "$HERE/installd/src/biosboot.c" \
   "$HERE/installd/src/parttable.c" "$HERE/installd/src/util.c"
cp -r grub/out/i386-pc root/pc-grub/
touch root/lineage/install-aaaa.id
cat > root/pc-grub/grub.cfg <<'CFG'
serial --unit=0 --speed=115200
terminal_input serial
terminal_output serial
echo PCGRUB-MENU-OK
search --no-floppy --set=root --file /lineage/install-aaaa.id
echo FOUND-$root
halt
CFG
failed=0
check() {
  local label=$1 partition=$2 start=$3
  rm -f disk.img part.img mbr.bak
  truncate -s 64M disk.img
  eval "$partition" >/dev/null 2>&1
  truncate -s 32M part.img
  mke2fs -q -F -t ext4 -d root part.img
  dd if=part.img of=disk.img bs=512 seek="$start" conv=notrunc status=none
  ./write disk.img grub/out/boot.img grub/out/core.img mbr.bak >/dev/null
  timeout 60 qemu-system-x86_64 -m 128 -nographic -no-reboot -drive file=disk.img,format=raw >"serial-$label.log" 2>&1 || true
  if grep -aq "FOUND-hd0" "serial-$label.log"; then echo "ok   $label"; else echo "FAIL $label"; failed=1; fi
}
check mbr-gap "printf 'label: dos\nstart=2048, size=65536, type=83\n' | sfdisk -q disk.img" 2048
check gpt-gap "sgdisk -o -n 1:2048:+32M -t 1:8300 disk.img" 2048
check gpt-bios-boot "sgdisk -o -n 1:2048:+1M -t 1:ef02 -n 2:4096:+32M -t 2:8300 disk.img" 4096
exit $failed
