#include "check.h"
#include "disks.h"
#include "fakesys.h"

static int count;
static struct partition parts[16];

static void setup_machine(void)
{
	fake_setup();
	fake_disk("sda", 512);
	fake_partition("sda", "sda1", 1, 65536);
	fake_partition("sda", "sda2", 2, 16777216);
	fake_partition("sda", "sda3", 3, 33554432);
	fake_sh("truncate -s 64M dev/block/%s && sgdisk -o -n 1:2048:+32M -t 1:ef00 -n 2:0:+8M -t 2:0700 "
		"-n 3:0:0 -t 3:8300 dev/block/sda >/dev/null", "sda");
	fake_device("sda1", "mkfs.vfat -n EFI");
	fake_signature("sda2", "xxxNTFS    ");
	fake_device("sda3", "mke2fs -q -F -t ext4 -L casa");
	fake_disk("sdb", 512);
	fake_partition("sdb", "sdb1", 1, 8192);
	fake_signature("sdb1", "xxxEXFAT   ");
	fake_sh("ln -s ../../devices/virtual/block/%1$s sys/class/block/%1$s && mkdir -p sys/devices/virtual/block/%1$s",
		"loop0");
	count = find_partitions(&fake_env, &FAKE_OPS, parts, 16);
}

static const struct partition *named(const char *name) { return find_partition(parts, count, name); }

static void lists_partitions_with_their_file_systems(void)
{
	setup_machine();
	CHECK(count == 4);
	CHECK_STR(named("sda1")->fs, "vfat");
	CHECK_STR(named("sda2")->fs, "ntfs");
	CHECK_STR(named("sda3")->fs, "ext4");
	CHECK_STR(named("sda3")->label, "casa");
	CHECK_STR(named("sda3")->disk, "sda");
	CHECK_STR(named("sdb1")->fs, "exfat");
	CHECK(named("sda3")->number == 3);
	CHECK(named("sda3")->size == 33554432ULL * 512);
	CHECK(named("loop0") == NULL);
	fake_teardown();
}

static void marks_the_esp_from_the_partition_table(void)
{
	setup_machine();
	CHECK(named("sda1")->is_esp);
	CHECK(!named("sda2")->is_esp && !named("sda3")->is_esp);
	fake_teardown();
}

static void measures_free_space_of_usable_partitions(void)
{
	setup_machine();
	CHECK(named("sda3")->free > 0);
	CHECK(named("sda2")->free > 0);
	fake_teardown();
}

static void lists_file_systems_on_whole_disks(void)
{
	setup_machine();
	fake_disk("sdc", 512);
	fake_sh("mkdir -p mnt-%s", "sdc");
	fake_device("sdc", "mke2fs -q -F -t ext4 -L inteiro");
	fake_disk("sdd", 512);
	fake_sh("truncate -s 8M dev/block/%s", "sdd");
	count = find_partitions(&fake_env, &FAKE_OPS, parts, 16);
	CHECK(named("sdc") != NULL);
	CHECK_STR(named("sdc")->fs, "ext4");
	CHECK_STR(named("sdc")->disk, "sdc");
	CHECK(named("sdc")->number == 0);
	CHECK(named("sdc")->free > 0);
	CHECK(named("sdd") == NULL);
	CHECK(named("sda") == NULL);
	fake_teardown();
}

static void prefers_the_esp_on_the_target_disk(void)
{
	struct partition list[3] = { { .name = "sdb1", .disk = "sdb", .is_esp = 1 },
				     { .name = "sda1", .disk = "sda", .is_esp = 1 },
				     { .name = "sda3", .disk = "sda" } };
	CHECK_STR(find_esp(list, 3, &list[2])->name, "sda1");
	struct partition other = { .name = "sdc1", .disk = "sdc" };
	CHECK_STR(find_esp(list, 3, &other)->name, "sdb1");
	CHECK(find_esp(list + 2, 1, &other) == NULL);
}

static void marks_every_partition_of_the_live_disk(void)
{
	struct partition list[3] = { { .name = "sdb1", .disk = "sdb" }, { .name = "sdb2", .disk = "sdb" },
				     { .name = "sda3", .disk = "sda" } };
	apply_source_mark(list, 3, "sdb");
	CHECK(list[0].is_source && list[1].is_source && !list[2].is_source);
	apply_source_mark(list, 3, "sda3");
	CHECK(list[2].is_source);
}

int main(void)
{
	RUN(lists_partitions_with_their_file_systems);
	RUN(marks_the_esp_from_the_partition_table);
	RUN(measures_free_space_of_usable_partitions);
	RUN(lists_file_systems_on_whole_disks);
	RUN(prefers_the_esp_on_the_target_disk);
	RUN(marks_every_partition_of_the_live_disk);
	return CHECK_DONE();
}
