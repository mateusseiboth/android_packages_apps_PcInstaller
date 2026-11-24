/* Other systems on the computer that the GRUB menu can start: by EFI loader, boot sector or menu. */
#pragma once
#include "grubcfg.h"

#define MAX_OTHERS 16

/* EFI loaders on a mounted ESP; uuid is the ESP's. */
int find_efi_systems(const char *esp_dir, const char *uuid, struct other_system *out, int max);
/* For BIOS: Windows (bootmgr) or another Linux's GRUB menu on a mounted partition named place. */
int find_bios_systems(const char *dir, const char *uuid, const char *place, struct other_system *out, int max);
