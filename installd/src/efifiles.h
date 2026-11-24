/*
 * The loaders we put on the ESP. EFI/LineageOS holds shim (Microsoft-signed), MokManager, our signed
 * GRUB and the builder's key; the fallback path EFI/BOOT gets shim as BOOTX64.EFI with GRUB and MokManager
 * next to it (shim loads them from its own directory), only when it is free or already ours.
 */
#pragma once

#define EFI_MENU_DIR "EFI/LineageOS"
#define EFI_FALLBACK_DIR "EFI/BOOT"

/* Copies the loaders from the live source folder into <esp>/EFI/LineageOS. */
int save_efi_files(const char *src_dir, const char *esp);
/* EFI/BOOT holds our loader: our GRUB as BOOTX64.EFI (older installs), or our GRUB next to shim. */
int is_our_fallback(const char *esp);
/* No BOOTX64.EFI yet, or ours; decide before save_efi_files replaces our GRUB. */
int is_fallback_free(const char *esp);
int save_fallback_files(const char *src_dir, const char *esp);
void remove_fallback_files(const char *esp);
