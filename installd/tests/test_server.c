#include <sys/stat.h>

#include "check.h"
#include "efiboot.h"
#include "mok.h"
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
	fake_sh("printf S > mnt-sdb1/%1$s/shimx64.efi && printf M > mnt-sdb1/%1$s/mmx64.efi"
		" && printf DER > mnt-sdb1/%1$s/ENROLL_THIS_KEY_IN_MOKMANAGER.der", "lineage");
	fake_sh("mkdir -p mnt-sdb1/%1$s/grub-pc/i386-pc && printf B > mnt-sdb1/%1$s/grub-pc/boot.img"
		" && printf C > mnt-sdb1/%1$s/grub-pc/core.img && touch mnt-sdb1/%1$s/install-live.id", "lineage");
	fake_backing_major = 8;
	fake_backing_minor = 17;
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
	CHECK_STR(field(0, "version"), "3");
	CHECK_STR(field(0, "uefi"), "1");
	CHECK_STR(field(0, "live"), "1");
	CHECK_STR(field(0, "folder"), "lineage");
	CHECK_STR(field(0, "system"), "1048576");
	CHECK_STR(field(0, "secure_boot"), "0");
	CHECK_STR(field(0, "mok_enrolled"), "0");
	CHECK_STR(field(0, "mok_queued"), "0");
	fake_teardown();
}

/* The key on the live source (the builder's own) is what shim must trust. */
static void save_moklist(const char *der)
{
	uint8_t list[256];
	size_t n = build_cert_list(list, sizeof list, (const uint8_t *)der, strlen(der));
	CHECK(!save_vendor_var(fake_env.efivars, SHIM_LOCK_GUID, "MokListRT", list, n));
}

static void info_tells_whether_shim_trusts_the_key(void)
{
	setup_machine();
	save_moklist("OTHER");
	send("INFO\n");
	CHECK_STR(field(0, "mok_enrolled"), "0");
	save_moklist("DER");
	send("INFO\n");
	CHECK_STR(field(0, "mok_enrolled"), "1");
	fake_teardown();
}

static void enroll_key_queues_the_key_for_mokmanager(void)
{
	setup_machine();
	CHECK_STR(send("ENROLL_KEY\n"), "OK\n");
	CHECK(reply_count == 2);
	CHECK_STR(field(0, "password"), "00000042");
	uint8_t *list = NULL;
	size_t len = 0;
	CHECK(!load_vendor_var(fake_env.efivars, SHIM_LOCK_GUID, "MokNew", &list, &len));
	CHECK(list && has_cert(list, len, (const uint8_t *)"DER", 3));
	free(list);
	send("INFO\n");
	CHECK_STR(field(0, "mok_queued"), "1");
	CHECK_STR(field(0, "mok_enrolled"), "0");
	fake_teardown();
}

static void enroll_key_needs_the_key_and_uefi(void)
{
	setup_machine();
	fake_sh("rm %s", "mnt-sdb1/lineage/ENROLL_THIS_KEY_IN_MOKMANAGER.der");
	CHECK_STR(send("ENROLL_KEY\n"), "ERR code=source_missing\n");
	fake_teardown();
	setup_machine();
	fake_sh("rmdir %s", "efivars");
	CHECK_STR(send("ENROLL_KEY\n"), "ERR code=efi_failed\n");
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

static void a_bios_computer_installs_when_the_disk_has_room(void)
{
	setup_machine();
	fake_sh("rmdir %s", "efivars");
	CHECK_STR(send("CHECK part=sda3 folder=lineage system=image data=folder\n"), "OK\n");
	CHECK_STR(field(0, "boot"), "bios");
	send("INFO\n");
	CHECK_STR(field(0, "uefi"), "0");
	fake_teardown();
}

static void uninstall_removes_an_install(void)
{
	setup_machine();
	CHECK_STR(send("INSTALL part=sda3 folder=lineage system=image data=folder\n"), "OK\n");
	CHECK_STR(send("UNINSTALL part=sda3 folder=lineage\n"), "OK\n");
	CHECK(find_reply("PROGRESS", "percent", "100") >= 0);
	struct stat st;
	CHECK(stat(fake_path("mnt-sda3/lineage"), &st));
	CHECK_STR(send("UNINSTALL part=sda3 folder=lineage\n"), "ERR code=not_installed\n");
	CHECK_STR(send("UNINSTALL part=sda3 folder=../x\n"), "ERR code=bad_folder\n");
	fake_teardown();
}

/* The same machine after a reboot into the installed system, with the USB drive still plugged in. */
static void setup_installed(void)
{
	setup_machine();
	fake_sh("mkdir -p mnt-sda3/lineage/userdata && touch mnt-sda3/lineage/install-aaaa.id %s",
		"mnt-sda3/lineage/system.img");
	fake_sh("ln -s ../../devices/pci/block/sda/sda3 sys/dev/block/%s", "8:3");
	fake_backing_minor = 3;
	fake_write("cmdline", "pc.src=lineage pc.id=aaaa quiet\n");
	fake_write("build.prop", "ro.build.date.utc=100\nro.lineage.display.version=24.0-old\n");
	fake_partition("sdb", "loop9", 0, 0);
	fake_sh("mkdir -p mnt-loop9/system && printf 'ro.build.date.utc=200\nro.lineage.display.version=24.0-new\n' > %s",
		"mnt-loop9/system/build.prop");
	server_init(&server, &fake_env, &FAKE_OPS);
}

static void an_installed_system_says_which_install_it_is(void)
{
	setup_installed();
	CHECK_STR(send("INFO\n"), "OK\n");
	CHECK_STR(field(0, "live"), "0");
	CHECK_STR(field(0, "installed"), "1");
	CHECK_STR(field(0, "install_part"), "sda3");
	CHECK_STR(field(0, "install_folder"), "lineage");
	CHECK_STR(field(0, "install_system"), "image");
	CHECK_STR(field(0, "install_version"), "24.0-old");
	fake_teardown();
}

static void an_installed_system_updates_from_a_usb_drive(void)
{
	setup_installed();
	CHECK_STR(send("FIND_UPDATE\n"), "OK\n");
	CHECK_STR(field(0, "source"), "sdb1");
	CHECK_STR(field(0, "version"), "24.0-new");
	CHECK_STR(field(0, "newer"), "1");
	CHECK_STR(field(0, "mok_enrolled"), "0");
	CHECK_STR(field(0, "mok_queued"), "0");
	CHECK_STR(send("INSTALL part=sda3 folder=lineage system=image data=keep\n"), "OK\n");
	struct stat st;
	CHECK(!stat(fake_path("mnt-sda3/lineage/system.old.img"), &st));
	CHECK(!stat(fake_path("mnt-sda3/lineage/install-aaaa.id"), &st)); /* the same menu entry */
	fake_teardown();
}

static void the_running_install_cannot_be_removed(void)
{
	setup_installed();
	CHECK_STR(send("UNINSTALL part=sda3 folder=lineage\n"), "ERR code=running_system\n");
	fake_teardown();
}

static void without_a_usb_drive_there_is_no_update(void)
{
	setup_installed();
	fake_sh("rm %s", "mnt-sdb1/lineage/install-live.id");
	CHECK_STR(send("FIND_UPDATE\n"), "ERR code=no_update_source\n");
	fake_teardown();
}

int main(void)
{
	RUN(a_bios_computer_installs_when_the_disk_has_room);
	RUN(uninstall_removes_an_install);
	RUN(an_installed_system_says_which_install_it_is);
	RUN(an_installed_system_updates_from_a_usb_drive);
	RUN(the_running_install_cannot_be_removed);
	RUN(without_a_usb_drive_there_is_no_update);
	RUN(info_tells_the_firmware_and_the_source);
	RUN(info_tells_whether_shim_trusts_the_key);
	RUN(enroll_key_queues_the_key_for_mokmanager);
	RUN(enroll_key_needs_the_key_and_uefi);
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
