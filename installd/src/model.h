/* What pc-installd knows about disks, the live source and an existing install. */
#pragma once

struct partition {
	char name[32];  /* sda3, nvme0n1p2 */
	char disk[32];  /* sda, nvme0n1 */
	char fs[16];    /* see fsprobe.h */
	char label[64];
	char uuid[40];  /* as GRUB's search --fs-uuid wants it; empty when unknown */
	unsigned long long size;
	unsigned long long free; /* 0 when it could not be mounted to measure */
	int number;              /* partition number on its disk */
	int is_esp;
	int is_source;           /* holds the live system we are running from */
	int cannot_mount;        /* a read-only mount failed (e.g. Windows hibernated or Fast Startup) */
};

/* Bytes of what gets copied from the live source. */
struct source_sizes {
	unsigned long long kernel, initrd, system, grub, core; /* core: GRUB's BIOS core.img */
};

/* What already sits in <partition>/<folder>. */
struct install_state {
	int has_system;
	int has_data;
	unsigned long long old_system_bytes; /* system.old.img or system.old/, removed by an update */
	int is_image;                        /* system.img rather than system/ */
	char id[24];                         /* from install-<id>.id; an update keeps it */
};

/* How this computer boots. */
struct machine {
	int is_uefi;       /* started by UEFI firmware (efivarfs is there); else BIOS */
	int has_esp;       /* UEFI: an ESP to hold GRUB */
	int has_boot_room; /* BIOS: room for GRUB's core image on the target disk */
};
