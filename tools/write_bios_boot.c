/* Writes GRUB for BIOS to a disk image with pc-installd's biosboot.c: DISK BOOT_IMG CORE_IMG BACKUP. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "biosboot.h"
int main(int argc, char **argv)
{
	static uint8_t head[PARTTABLE_HEAD_BYTES];
	struct part_table t;
	struct stat st;
	uint64_t start;
	int fd = open(argv[1], O_RDONLY);
	if (pread(fd, head, sizeof head, 0) != sizeof head || parse_part_table(head, sizeof head, 512, &t)) return 2;
	close(fd);
	stat(argv[3], &st);
	if (find_core_place(&t, 512, (st.st_size + 511) / 512, &start)) return 3;
	printf("core at %llu\n", (unsigned long long)start);
	return write_bios_boot(argv[1], argv[2], argv[3], start, argv[4]) ? 4 : 0;
}
