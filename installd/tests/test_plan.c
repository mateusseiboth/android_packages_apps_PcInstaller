#include "check.h"
#include "plan.h"

#define GIB (1ULL << 30)
#define MIB (1ULL << 20)

static const struct source_sizes SOURCE = { .kernel = 16 * MIB, .initrd = 1 * MIB, .system = 3 * GIB, .grub = 4 * MIB };

static struct partition part(const char *fs, unsigned long long free_bytes)
{
	struct partition p = { .name = "sda3", .free = free_bytes, .size = free_bytes * 2 };
	snprintf(p.fs, sizeof p.fs, "%s", fs);
	return p;
}

static struct plan_request request(const char *system, const char *data, unsigned long long data_size)
{
	return (struct plan_request){ .folder = "lineage", .system_form = system, .data_form = data, .data_size = data_size };
}

static const char *check(struct plan_request r, struct partition p, struct install_state s)
{
	struct plan_result out;
	return validate_plan(&r, &p, &SOURCE, &s, &out) ? out.error : "ok";
}

static const struct install_state FRESH = { 0 };

static void linux_file_systems_take_every_form(void)
{
	CHECK_STR(check(request("folder", "folder", 0), part("ext4", 100 * GIB), FRESH), "ok");
	CHECK_STR(check(request("folder", "image", 8 * GIB), part("btrfs", 100 * GIB), FRESH), "ok");
	CHECK_STR(check(request("image", "folder", 0), part("f2fs", 100 * GIB), FRESH), "ok");
}

static void windows_file_systems_take_only_images(void)
{
	CHECK_STR(check(request("image", "image", 8 * GIB), part("ntfs", 100 * GIB), FRESH), "ok");
	CHECK_STR(check(request("folder", "image", 8 * GIB), part("ntfs", 100 * GIB), FRESH), "fs_images_only");
	CHECK_STR(check(request("image", "folder", 0), part("exfat", 100 * GIB), FRESH), "fs_images_only");
}

static void fat32_cannot_hold_files_of_4_gib(void)
{
	CHECK_STR(check(request("image", "image", 3 * GIB), part("vfat", 100 * GIB), FRESH), "ok");
	CHECK_STR(check(request("image", "image", 4 * GIB), part("vfat", 100 * GIB), FRESH), "file_too_large");
}

static void refuses_partitions_that_cannot_hold_an_install(void)
{
	CHECK_STR(check(request("image", "image", 8 * GIB), part("bitlocker", 100 * GIB), FRESH), "bitlocker");
	CHECK_STR(check(request("image", "image", 8 * GIB), part("luks", 100 * GIB), FRESH), "encrypted");
	CHECK_STR(check(request("image", "image", 8 * GIB), part("swap", 100 * GIB), FRESH), "fs_unsupported");
	CHECK_STR(check(request("image", "image", 8 * GIB), part("unknown", 100 * GIB), FRESH), "fs_unsupported");
	struct partition live = part("iso9660", 100 * GIB);
	live.is_source = 1;
	CHECK_STR(check(request("image", "image", 8 * GIB), live, FRESH), "source_partition");
}

static void needs_room_for_everything_plus_a_margin(void)
{
	struct plan_request r = request("image", "image", 8 * GIB);
	unsigned long long need = SOURCE.kernel + SOURCE.initrd + SOURCE.system + 8 * GIB + PLAN_MARGIN;
	struct plan_result out;
	struct partition p = part("ext4", need);
	CHECK(!validate_plan(&r, &p, &SOURCE, &FRESH, &out));
	CHECK(out.need == need);
	CHECK_STR(check(r, part("ext4", need - 1), FRESH), "no_space");
}

static void the_data_image_has_a_minimum_size(void)
{
	CHECK_STR(check(request("image", "image", PLAN_MIN_DATA - 1), part("ext4", 100 * GIB), FRESH), "data_too_small");
}

static void an_existing_install_is_updated_and_keeps_its_data(void)
{
	struct install_state old = { .has_system = 1, .has_data = 1 };
	struct plan_result out;
	struct plan_request r = request("image", "keep", 0);
	struct partition p = part("ext4", 100 * GIB);
	CHECK(!validate_plan(&r, &p, &SOURCE, &old, &out));
	CHECK_STR(out.mode, "update");
	CHECK_STR(check(request("image", "image", 8 * GIB), p, old), "data_exists");
	CHECK_STR(check(request("image", "keep", 0), p, FRESH), "no_data_to_keep");
}

static void the_previous_system_frees_its_space_on_update(void)
{
	struct install_state old = { .has_system = 1, .has_data = 1, .old_system_bytes = 3 * GIB };
	unsigned long long need = SOURCE.kernel + SOURCE.initrd + SOURCE.system + PLAN_MARGIN;
	CHECK_STR(check(request("image", "keep", 0), part("ext4", need - 3 * GIB), old), "ok");
}

static void a_fresh_install_says_install(void)
{
	struct plan_result out;
	struct plan_request r = request("folder", "folder", 0);
	struct partition p = part("ext4", 100 * GIB);
	CHECK(!validate_plan(&r, &p, &SOURCE, &FRESH, &out));
	CHECK_STR(out.mode, "install");
}

static void folder_names_stay_inside_the_partition(void)
{
	static const char *const GOOD[] = { "lineage", "android/lineage-24", "LineageOS_PC" };
	static const char *const BAD[] = { "", "/lineage", "../x", "a/../b", "a//b", "a/", "com espaco",
					   "acentuação", "a\\b" };
	for (size_t i = 0; i < sizeof GOOD / sizeof *GOOD; i++)
		CHECK(is_valid_folder(GOOD[i]));
	for (size_t i = 0; i < sizeof BAD / sizeof *BAD; i++)
		CHECK(!is_valid_folder(BAD[i]));
	struct plan_request r = request("image", "folder", 0);
	snprintf(r.folder, sizeof r.folder, "../etc");
	CHECK_STR(check(r, part("ext4", 100 * GIB), FRESH), "bad_folder");
}

static void unknown_forms_are_a_bad_request(void)
{
	CHECK_STR(check(request("zip", "folder", 0), part("ext4", 100 * GIB), FRESH), "bad_request");
	CHECK_STR(check(request("image", "cloud", 0), part("ext4", 100 * GIB), FRESH), "bad_request");
}

static void every_file_system_has_a_rule(void)
{
	CHECK_STR(find_fs_rule("ntfs")->mount_type, "ntfs3");
	CHECK(find_fs_rule("ext4")->allows_folders);
	CHECK(!find_fs_rule("exfat")->allows_folders);
	CHECK_STR(find_fs_rule("never-heard-of-it")->reason, "fs_unsupported");
}

int main(void)
{
	RUN(linux_file_systems_take_every_form);
	RUN(windows_file_systems_take_only_images);
	RUN(fat32_cannot_hold_files_of_4_gib);
	RUN(refuses_partitions_that_cannot_hold_an_install);
	RUN(needs_room_for_everything_plus_a_margin);
	RUN(the_data_image_has_a_minimum_size);
	RUN(an_existing_install_is_updated_and_keeps_its_data);
	RUN(the_previous_system_frees_its_space_on_update);
	RUN(a_fresh_install_says_install);
	RUN(folder_names_stay_inside_the_partition);
	RUN(unknown_forms_are_a_bad_request);
	RUN(every_file_system_has_a_rule);
	return CHECK_DONE();
}
