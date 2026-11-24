#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "menus.h"
#include "treecopy.h"

static char dir[64];

static void setup(void)
{
	snprintf(dir, sizeof dir, "/tmp/menu-XXXXXX");
	CHECK(mkdtemp(dir) != NULL);
}

static void teardown(void) { remove_tree(dir); }

static const char *read_menu(void)
{
	static char text[8192];
	char path[128];
	snprintf(path, sizeof path, "%s/grub.cfg", dir);
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
	text[n] = 0;
	if (f)
		fclose(f);
	return text;
}

static void the_menu_holds_every_saved_entry(void)
{
	setup();
	CHECK(!save_menu_entry(dir, "bb", "menuentry \"LineageOS (B)\" --id lineage-bb {\n}\n"));
	CHECK(!save_menu_entry(dir, "aa", "menuentry \"LineageOS (A)\" --id lineage-aa {\n}\n"));
	CHECK(count_menu_entries(dir) == 2);
	CHECK(!rebuild_menu(dir, "menuentry \"Windows Boot Manager\" {\n}\n", "bb", 1));
	const char *m = read_menu();
	const char *a = strstr(m, "LineageOS (A)"), *b = strstr(m, "LineageOS (B)");
	CHECK(a && b && a < b);
	CHECK(strstr(m, "set default=lineage-bb\n") != NULL);
	CHECK(strstr(m, "Windows Boot Manager") != NULL);
	teardown();
}

static void saving_again_replaces_only_that_install(void)
{
	setup();
	save_menu_entry(dir, "aa", "menuentry \"LineageOS (A)\" --id lineage-aa {\n}\n");
	save_menu_entry(dir, "bb", "menuentry \"LineageOS (B)\" --id lineage-bb {\n}\n");
	save_menu_entry(dir, "aa", "menuentry \"LineageOS (A2)\" --id lineage-aa {\n}\n");
	rebuild_menu(dir, "", "aa", 0);
	const char *m = read_menu();
	CHECK(strstr(m, "LineageOS (A2)") && strstr(m, "LineageOS (B)") && !strstr(m, "LineageOS (A)"));
	teardown();
}

static void removing_an_install_drops_its_entry(void)
{
	setup();
	save_menu_entry(dir, "aa", "menuentry \"LineageOS (A)\" --id lineage-aa {\n}\n");
	save_menu_entry(dir, "bb", "menuentry \"LineageOS (B)\" --id lineage-bb {\n}\n");
	CHECK(!remove_menu_entry(dir, "aa"));
	CHECK(count_menu_entries(dir) == 1);
	CHECK(!rebuild_menu(dir, "", "aa", 0));
	const char *m = read_menu();
	CHECK(!strstr(m, "LineageOS (A)") && strstr(m, "LineageOS (B)"));
	CHECK(strstr(m, "set default=lineage-bb\n") != NULL);
	CHECK(!remove_menu_entry(dir, "deadbeef"));
	teardown();
}

static void ids_cannot_leave_the_directory(void)
{
	setup();
	CHECK(save_menu_entry(dir, "../x", "menuentry {\n}\n"));
	CHECK(remove_menu_entry(dir, "a/b"));
	teardown();
}

static void an_empty_directory_has_no_entries(void)
{
	setup();
	CHECK(count_menu_entries(dir) == 0);
	CHECK(count_menu_entries("/nonexistent/menu") == 0);
	teardown();
}

int main(void)
{
	RUN(the_menu_holds_every_saved_entry);
	RUN(saving_again_replaces_only_that_install);
	RUN(removing_an_install_drops_its_entry);
	RUN(ids_cannot_leave_the_directory);
	RUN(an_empty_directory_has_no_entries);
	return CHECK_DONE();
}
