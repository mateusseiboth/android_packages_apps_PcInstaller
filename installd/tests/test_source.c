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
	fake_disk("sdb", 512);
	fake_partition("sdb", "sdb1", 1, 8192);
	fake_sh("mkdir -p sys/dev/block && ln -s ../../devices/pci/block/sdb/sdb1 sys/dev/block/%s", "8:17");
	fake_write("mountinfo", MOUNTINFO);
	fake_write("cmdline", "BOOT_IMAGE=/lineage/kernel pc.src=lineage pc.live=1 quiet\n");
	fake_sh("mkdir -p mnt-sdb1/%1$s && printf 12345678 > mnt-sdb1/%1$s/kernel && printf 1234 > mnt-sdb1/%1$s/initrd.img"
		" && truncate -s 3G mnt-sdb1/%1$s/system.img && printf 12 > mnt-sdb1/%1$s/grubx64.efi", "lineage");
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

int main(void)
{
	RUN(finds_the_root_device_in_mountinfo);
	RUN(reads_the_folder_and_live_mode_from_the_cmdline);
	RUN(finds_and_mounts_the_live_source);
	RUN(an_installed_system_is_not_a_live_source);
	RUN(a_source_without_the_files_cannot_be_used);
	return CHECK_DONE();
}
