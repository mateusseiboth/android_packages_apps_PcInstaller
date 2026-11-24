#include "check.h"
#include "fakesys.h"
#include "source.h"

static const char MOUNTINFO[] =
	"1 0 7:0 / / ro,relatime - ext4 /dev/loop0 ro\n"
	"20 1 0:5 / /dev rw,nosuid - tmpfs tmpfs rw\n"
	"31 1 7:3 / /data rw - ext4 /dev/loop3 rw\n";

static void setup_live(void)
{
	fake_setup();
	fake_backing_major = 8;
	fake_backing_minor = 17;
	fake_disk("sdb", 512);
	fake_partition("sdb", "sdb1", 1, 8192);
	fake_sh("mkdir -p sys/dev/block && ln -s ../../devices/pci/block/sdb/sdb1 sys/dev/block/%s", "8:17");
	fake_write("mountinfo", MOUNTINFO);
	fake_write("cmdline", "BOOT_IMAGE=/lineage/kernel pc.src=lineage pc.live=1 quiet\n");
	fake_sh("mkdir -p mnt-sdb1/%1$s && printf 12345678 > mnt-sdb1/%1$s/kernel && printf 1234 > mnt-sdb1/%1$s/initrd.img"
		" && truncate -s 3G mnt-sdb1/%1$s/system.img && printf 12 > mnt-sdb1/%1$s/grubx64.efi"
		" && mkdir -p mnt-sdb1/%1$s/grub-pc/i386-pc && printf 1 > mnt-sdb1/%1$s/grub-pc/boot.img"
		" && printf 123 > mnt-sdb1/%1$s/grub-pc/core.img && touch mnt-sdb1/%1$s/install-live.id", "lineage");
	fake_sh("printf 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' > %s", "dev/block/sdb1");
}

static void finds_the_root_device_in_mountinfo(void)
{
	unsigned major = 0, minor = 0;
	CHECK(!parse_root_device(MOUNTINFO, &major, &minor));
	CHECK(major == 7 && minor == 0);
	CHECK(parse_root_device("20 1 0:5 / /dev rw - tmpfs tmpfs rw\n", &major, &minor));
}

static void reads_the_folder_and_live_mode_from_the_cmdline(void)
{
	char folder[64];
	CHECK(parse_live_cmdline("pc.src=os/lineage pc.live=1", folder, sizeof folder));
	CHECK_STR(folder, "os/lineage");
	CHECK(!parse_live_cmdline("quiet", folder, sizeof folder));
	CHECK_STR(folder, "lineage");
}

static void finds_and_mounts_the_live_source(void)
{
	setup_live();
	struct live_source s;
	CHECK(!find_live_source(&fake_env, &FAKE_OPS, &s));
	CHECK_STR(s.name, "sdb1");
	CHECK_STR(s.disk, "sdb");
	CHECK_STR(s.folder, "lineage");
	CHECK(!mount_live_source(&fake_env, &FAKE_OPS, &s));
	CHECK(s.sizes.kernel == 8 && s.sizes.initrd == 4 && s.sizes.system == 3ULL << 30 && s.sizes.grub == 2);
	CHECK(s.sizes.core == 3);
	char want[256];
	snprintf(want, sizeof want, "%s/src/lineage", fake_env.work);
	CHECK_STR(s.dir, want);
	fake_teardown();
}

static void an_installed_system_is_not_a_live_source(void)
{
	setup_live();
	fake_write("cmdline", "pc.src=lineage quiet\n");
	struct live_source s;
	CHECK(find_live_source(&fake_env, &FAKE_OPS, &s));
	fake_teardown();
}

static void a_source_without_the_files_cannot_be_used(void)
{
	setup_live();
	fake_sh("rm %s", "mnt-sdb1/lineage/system.img");
	struct live_source s;
	CHECK(!find_live_source(&fake_env, &FAKE_OPS, &s));
	CHECK(mount_live_source(&fake_env, &FAKE_OPS, &s));
	fake_teardown();
}

static void reads_the_build_of_a_system(void)
{
	struct build_info b;
	parse_build_prop("# begin\nro.build.date.utc=1700000000\nro.lineage.display.version=24.0-20261124-PC\n", &b);
	CHECK(b.utc == 1700000000ULL);
	CHECK_STR(b.version, "24.0-20261124-PC");
	parse_build_prop("ro.build.display.id=AP4A.1\nro.build.date.utc=5\n", &b);
	CHECK_STR(b.version, "AP4A.1");
	parse_build_prop("", &b);
	CHECK(b.utc == 0 && !*b.version);
}

/* An installed system: / is system.img on sda3 through loop0, no pc.live. */
static void setup_installed(void)
{
	fake_setup();
	fake_disk("sda", 512);
	fake_partition("sda", "sda3", 3, 8192);
	fake_disk("sdb", 512);
	fake_partition("sdb", "sdb1", 1, 8192);
	fake_partition("sdb", "loop9", 0, 0);
	fake_sh("mkdir -p sys/dev/block && ln -s ../../devices/pci/block/sda/sda3 sys/dev/block/%s", "8:3");
	fake_backing_major = 8;
	fake_backing_minor = 3;
	fake_write("mountinfo", MOUNTINFO);
	fake_write("cmdline", "pc.src=os/lineage pc.id=aaaa quiet\n");
	fake_write("build.prop", "ro.build.date.utc=100\nro.lineage.display.version=old\n");
	fake_sh("mkdir -p mnt-sda3/os/lineage && touch mnt-sda3/os/lineage/install-aaaa.id mnt-sda3/%s", "os/lineage/system.img");
	fake_sh("mkdir -p mnt-sdb1/%1$s/grub-pc/i386-pc && touch mnt-sdb1/%1$s/kernel mnt-sdb1/%1$s/initrd.img"
		" mnt-sdb1/%1$s/system.img mnt-sdb1/%1$s/grubx64.efi mnt-sdb1/%1$s/grub-pc/boot.img"
		" mnt-sdb1/%1$s/grub-pc/core.img mnt-sdb1/%1$s/install-live.id", "lineage");
	fake_sh("mkdir -p mnt-loop9/system && printf 'ro.build.date.utc=200\nro.lineage.display.version=new\n' > %s",
		"mnt-loop9/system/build.prop");
}

static void an_installed_system_knows_where_it_runs_from(void)
{
	setup_installed();
	struct running_install run;
	CHECK(!find_running_install(&fake_env, &FAKE_OPS, &run));
	CHECK_STR(run.part, "sda3");
	CHECK_STR(run.folder, "os/lineage");
	CHECK_STR(run.id, "aaaa");
	CHECK(run.build.utc == 100 && !strcmp(run.build.version, "old"));
	fake_teardown();
	setup_live();
	CHECK(find_running_install(&fake_env, &FAKE_OPS, &run));
	fake_teardown();
}

static void finds_a_newer_system_on_a_usb_drive(void)
{
	setup_installed();
	struct partition parts[2] = { { .name = "sda3", .disk = "sda", .fs = "ext4" },
				      { .name = "sdb1", .disk = "sdb", .fs = "vfat" } };
	struct live_source src;
	struct build_info build;
	CHECK(!find_usb_source(&fake_env, &FAKE_OPS, parts, 2, "sda3", &src));
	CHECK_STR(src.name, "sdb1");
	CHECK_STR(src.folder, "lineage");
	CHECK(!read_source_build(&fake_env, &FAKE_OPS, &src, &build));
	CHECK(build.utc == 200 && !strcmp(build.version, "new"));
	fake_teardown();
}

static void a_drive_without_the_live_system_is_not_an_update(void)
{
	setup_installed();
	fake_sh("rm %s", "mnt-sdb1/lineage/install-live.id");
	struct partition parts[2] = { { .name = "sda3", .disk = "sda", .fs = "ext4" },
				      { .name = "sdb1", .disk = "sdb", .fs = "vfat" } };
	struct live_source src;
	CHECK(find_usb_source(&fake_env, &FAKE_OPS, parts, 2, "sda3", &src));
	fake_teardown();
}

int main(void)
{
	RUN(reads_the_build_of_a_system);
	RUN(an_installed_system_knows_where_it_runs_from);
	RUN(finds_a_newer_system_on_a_usb_drive);
	RUN(a_drive_without_the_live_system_is_not_an_update);
	RUN(finds_the_root_device_in_mountinfo);
	RUN(reads_the_folder_and_live_mode_from_the_cmdline);
	RUN(finds_and_mounts_the_live_source);
	RUN(an_installed_system_is_not_a_live_source);
	RUN(a_source_without_the_files_cannot_be_used);
	return CHECK_DONE();
}
