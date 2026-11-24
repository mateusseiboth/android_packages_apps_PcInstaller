#include "check.h"
#include "fakesys.h"
#include "proto.h"
#include "server.h"

static char replies[64][LINE_MAX_LEN];
static int reply_count;

static void collect(void *ctx, const char *line)
{
	(void)ctx;
	if (reply_count < 64)
		snprintf(replies[reply_count++], LINE_MAX_LEN, "%s", line);
}

static struct server server;

static void setup_machine(void)
{
	fake_setup();
	fake_disk("sda", 512);
	fake_partition("sda", "sda1", 1, 65536);
	fake_partition("sda", "sda3", 3, 33554432);
	fake_sh("truncate -s 64M dev/block/%s && sgdisk -o -n 1:2048:+32M -t 1:ef00 -n 3:0:0 -t 3:8300 "
		"dev/block/sda >/dev/null", "sda");
	fake_device("sda1", "mkfs.vfat");
	fake_device("sda3", "mke2fs -q -F -t ext4");
	fake_disk("sdb", 512);
	fake_partition("sdb", "sdb1", 1, 8192);
	fake_device("sdb1", "mkfs.vfat -n LIVE");
	fake_sh("mkdir -p sys/dev/block && ln -s ../../devices/pci/block/sdb/sdb1 sys/dev/block/%s", "8:17");
	fake_write("mountinfo", "1 0 7:0 / / ro - ext4 /dev/loop0 ro\n");
	fake_write("cmdline", "pc.src=lineage pc.live=1\n");
	fake_sh("mkdir -p mnt-sdb1/%1$s && printf K > mnt-sdb1/%1$s/kernel && printf I > mnt-sdb1/%1$s/initrd.img"
		" && printf G > mnt-sdb1/%1$s/grubx64.efi && truncate -s 1M mnt-sdb1/%1$s/system.img", "lineage");
	server_init(&server, &fake_env, &FAKE_OPS);
	reply_count = 0;
}

static const char *send(const char *line)
{
	reply_count = 0;
	handle_line(&server, line, collect, NULL);
	return reply_count ? replies[reply_count - 1] : "";
}

static const char *field(int i, const char *key)
{
	static struct msg m;
	return parse_msg(replies[i], &m) ? "" : find_field(&m, key) ? find_field(&m, key) : "";
}

static int find_reply(const char *word, const char *key, const char *value)
{
	for (int i = 0; i < reply_count; i++) {
		struct msg m;
		if (!parse_msg(replies[i], &m) && !strcmp(m.word, word) && find_field(&m, key) &&
		    !strcmp(find_field(&m, key), value))
			return i;
	}
	return -1;
}

static void info_tells_the_firmware_and_the_source(void)
{
	setup_machine();
	CHECK_STR(send("INFO\n"), "OK\n");
	CHECK(reply_count == 2);
	CHECK_STR(field(0, "version"), "1");
	CHECK_STR(field(0, "uefi"), "1");
	CHECK_STR(field(0, "live"), "1");
	CHECK_STR(field(0, "folder"), "lineage");
	CHECK_STR(field(0, "system"), "1048576");
	fake_teardown();
}

static void list_describes_each_partition(void)
{
	setup_machine();
	CHECK_STR(send("LIST\n"), "OK\n");
	int sda3 = find_reply("PART", "name", "sda3");
	CHECK(sda3 >= 0);
	CHECK_STR(field(sda3, "fs"), "ext4");
	CHECK_STR(field(sda3, "usable"), "1");
	CHECK_STR(field(sda3, "folders"), "1");
	CHECK_STR(field(sda3, "source"), "0");
	int live = find_reply("PART", "name", "sdb1");
	CHECK(live >= 0);
	CHECK_STR(field(live, "source"), "1");
	CHECK_STR(field(live, "label"), "LIVE");
	CHECK_STR(field(find_reply("PART", "name", "sda1"), "esp"), "1");
	fake_teardown();
}

static void a_partition_that_cannot_be_opened_says_so(void)
{
	setup_machine();
	fake_sh("rmdir %s", "mnt-sda3");
	send("LIST\n");
	int sda3 = find_reply("PART", "name", "sda3");
	CHECK_STR(field(sda3, "usable"), "0");
	CHECK_STR(field(sda3, "reason"), "mount_failed");
	fake_teardown();
}

static void check_validates_a_plan(void)
{
	setup_machine();
	CHECK_STR(send("CHECK part=sda3 folder=lineage system=folder data=image data_size=2147483648\n"), "OK\n");
	CHECK_STR(field(0, "mode"), "install");
	CHECK_STR(field(0, "esp"), "sda1");
	CHECK_STR(send("CHECK part=sdb1 folder=lineage system=image data=image data_size=2147483648\n"),
		  "ERR code=source_partition\n");
	CHECK_STR(send("CHECK part=nope folder=lineage system=image data=image\n"), "ERR code=unknown_partition\n");
	fake_teardown();
}

static void inspect_reports_an_existing_install(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda3/lineage/userdata && touch %s", "mnt-sda3/lineage/system.img");
	CHECK_STR(send("INSPECT part=sda3 folder=lineage\n"), "OK\n");
	CHECK_STR(field(0, "system"), "1");
	CHECK_STR(field(0, "data"), "1");
	CHECK_STR(send("INSPECT part=sda3 folder=../x\n"), "ERR code=bad_folder\n");
	fake_teardown();
}

static void install_streams_progress_and_finishes(void)
{
	setup_machine();
	CHECK_STR(send("INSTALL part=sda3 folder=lineage system=image data=folder\n"), "OK\n");
	CHECK(find_reply("PROGRESS", "percent", "100") >= 0);
	CHECK(find_reply("PROGRESS", "step", "system") >= 0);
	struct stat st;
	CHECK(!stat(fake_path("mnt-sda3/lineage/system.img"), &st));
	CHECK(!stat(fake_path("mnt-sda1/EFI/LineageOS/grub.cfg"), &st));
	fake_teardown();
}

static void install_refuses_an_invalid_plan(void)
{
	setup_machine();
	CHECK_STR(send("INSTALL part=sda3 folder=lineage system=image data=image data_size=1\n"),
		  "ERR code=data_too_small\n");
	CHECK(reply_count == 1);
	fake_teardown();
}

static void unknown_or_malformed_requests_are_refused(void)
{
	setup_machine();
	CHECK_STR(send("FORMAT part=sda3\n"), "ERR code=bad_request\n");
	CHECK_STR(send("garbage\n"), "ERR code=bad_request\n");
	fake_teardown();
}

static void without_a_live_source_nothing_installs(void)
{
	setup_machine();
	fake_write("cmdline", "pc.src=lineage\n");
	server_init(&server, &fake_env, &FAKE_OPS);
	CHECK_STR(send("CHECK part=sda3 folder=lineage system=image data=folder\n"), "ERR code=source_missing\n");
	send("INFO\n");
	CHECK_STR(field(0, "live"), "0");
	fake_teardown();
}

int main(void)
{
	RUN(info_tells_the_firmware_and_the_source);
	RUN(list_describes_each_partition);
	RUN(a_partition_that_cannot_be_opened_says_so);
	RUN(check_validates_a_plan);
	RUN(inspect_reports_an_existing_install);
	RUN(install_streams_progress_and_finishes);
	RUN(install_refuses_an_invalid_plan);
	RUN(unknown_or_malformed_requests_are_refused);
	RUN(without_a_live_source_nothing_installs);
	return CHECK_DONE();
}
