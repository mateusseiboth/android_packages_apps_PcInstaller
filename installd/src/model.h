/* What pc-installd knows about disks, the live source and an existing install. */
#pragma once

struct partition {
	char name[32];  /* sda3, nvme0n1p2 */
	char disk[32];  /* sda, nvme0n1 */
	char fs[16];    /* see fsprobe.h */
	char label[64];
	unsigned long long size;
	unsigned long long free; /* 0 when it could not be mounted to measure */
	int number;              /* partition number on its disk */
	int is_esp;
	int is_source;           /* holds the live system we are running from */
};

/* Bytes of what gets copied from the live source. */
struct source_sizes {
	unsigned long long kernel, initrd, system, grub;
};

/* What already sits in <partition>/<folder>. */
struct install_state {
	int has_system;
	int has_data;
	unsigned long long old_system_bytes; /* system.old.img or system.old/, removed by an update */
};

/* How this computer boots. */
struct machine {
	int is_uefi; /* the live system was started by UEFI firmware (efivarfs is there) */
	int has_esp;
};
