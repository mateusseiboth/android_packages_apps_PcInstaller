#include "grubcfg.h"

#include <stdio.h>
#include <string.h>

#include "layout.h"

/* androidboot.selinux=permissive until the vendor policy is complete. */
#define BASE_CMDLINE                                                                                \
	"androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware " \
	"console=tty0 loglevel=4"

void build_kernel_cmdline(char *out, size_t cap, const char *folder, const char *extra)
{
	snprintf(out, cap, "%s pc.src=%s%s%s", BASE_CMDLINE, folder, *extra ? " " : "", extra);
}

void build_id_file(char *out, size_t cap, const char *folder, const char *id)
{
	snprintf(out, cap, "%s/install-%s.id", folder, id);
}

/* ---- writer --------------------------------------------------------------------------- */

struct writer {
	char *out;
	size_t cap, len;
	int overflow;
};

static void put(struct writer *w, const char *text)
{
	int n = snprintf(w->out + w->len, w->cap - w->len, "%s", text);
	w->overflow |= n < 0 || (size_t)n >= w->cap - w->len;
	w->len = w->overflow ? w->len : w->len + (size_t)n;
}

static int finish(const struct writer *w) { return w->overflow ? -1 : 0; }

/* Labels come from the disks: no quote, dollar or backslash may reach GRUB's parser. */
static void build_title(char *out, size_t cap, const char *format, const char *place)
{
	char safe[96];
	snprintf(safe, sizeof safe, "%s", place);
	for (char *c = safe; *c; c++)
		*c = strchr("\"$\\", *c) ? '_' : *c;
	snprintf(out, cap, format, safe);
}

/* ---- an install's entry --------------------------------------------------------------- */

static int is_always(const struct entry_params *p) { (void)p; return 1; }
static int has_previous(const struct entry_params *p) { return p->previous & PREVIOUS_SYSTEM; }

static const char *find_kernel(const struct entry_params *p) { (void)p; return KERNEL_FILE; }
static const char *find_initrd(const struct entry_params *p) { (void)p; return INITRD_FILE; }

/* The kernel and initrd from before the last update, when it kept them. */
static const char *find_old_kernel(const struct entry_params *p)
{
	return p->previous & PREVIOUS_KERNEL ? KERNEL_OLD : KERNEL_FILE;
}

static const char *find_old_initrd(const struct entry_params *p)
{
	return p->previous & PREVIOUS_INITRD ? INITRD_OLD : INITRD_FILE;
}

/* The first variant is the main entry; the others go in its "more options" submenu. */
static const struct {
	const char *title;
	const char *extra;
	int (*is_wanted)(const struct entry_params *);
	const char *(*kernel)(const struct entry_params *);
	const char *(*initrd)(const struct entry_params *);
} VARIANTS[] = {
	{ "LineageOS (%s)", "quiet", is_always, find_kernel, find_initrd },
	{ "LineageOS (%s), safe graphics", "nomodeset", is_always, find_kernel, find_initrd },
	{ "LineageOS (%s), previous system", "pc.previous=1 quiet", has_previous, find_old_kernel, find_old_initrd },
};

static void put_variant(struct writer *w, const struct entry_params *p, size_t i, const char *indent)
{
	char title[160], args[256], cmdline[1024], id_file[256], text[2048];
	build_title(title, sizeof title, VARIANTS[i].title, p->title);
	snprintf(args, sizeof args, "pc.id=%s %s", p->id, VARIANTS[i].extra);
	build_kernel_cmdline(cmdline, sizeof cmdline, p->folder, args);
	build_id_file(id_file, sizeof id_file, p->folder, p->id);
	snprintf(text, sizeof text,
		 "%1$smenuentry \"%2$s\"%3$s%4$s {\n"
		 "%1$s    search --no-floppy --set=root --file /%5$s\n"
		 "%1$s    linux /%6$s/%8$s %7$s\n"
		 "%1$s    initrd /%6$s/%9$s\n"
		 "%1$s}\n",
		 indent, title, i ? "" : " --id lineage-", i ? "" : p->id, id_file, p->folder, cmdline,
		 VARIANTS[i].kernel(p), VARIANTS[i].initrd(p));
	put(w, text);
}

int build_install_entry(char *out, size_t cap, const struct entry_params *p)
{
	struct writer w = { out, cap, 0, 0 };
	char title[160], text[256];
	put_variant(&w, p, 0, "");
	build_title(title, sizeof title, "LineageOS (%s): more options", p->title);
	snprintf(text, sizeof text, "submenu \"%s\" --id lineage-%s-more {\n", title, p->id);
	put(&w, text);
	for (size_t i = 1; i < sizeof VARIANTS / sizeof *VARIANTS; i++)
		if (VARIANTS[i].is_wanted(p))
			put_variant(&w, p, i, "    ");
	put(&w, "}\n");
	return finish(&w);
}

/* ---- other systems -------------------------------------------------------------------- */

static const char *const OTHER_COMMANDS[] = {
	[OTHER_EFI] = "chainloader %s",
	[OTHER_BOOT_SECTOR] = "chainloader +1",
	[OTHER_CONFIGFILE] = "configfile %s",
};

int build_other_entry(char *out, size_t cap, const struct other_system *o)
{
	struct writer w = { out, cap, 0, 0 };
	char title[160], command[256], text[640];
	build_title(title, sizeof title, "%s", o->title);
	snprintf(command, sizeof command, OTHER_COMMANDS[o->kind], o->loader);
	snprintf(text, sizeof text, "menuentry \"%s\" {\n    search --no-floppy --set=root --fs-uuid %s\n    %s\n}\n\n",
		 title, o->uuid, command);
	put(&w, text);
	return finish(&w);
}

/* ---- the menu ------------------------------------------------------------------------- */

int build_menu(char *out, size_t cap, const struct menu_params *m)
{
	struct writer w = { out, cap, 0, 0 };
	char header[256];
	snprintf(header, sizeof header,
		 "# Written by pc-installd; rebuilt on every install, update or removal.\n"
		 "set timeout=5\nset default=lineage-%s\n\n", m->default_id);
	put(&w, header);
	for (int i = 0; i < m->count; i++) {
		put(&w, m->entries[i]);
		put(&w, "\n");
	}
	put(&w, m->others);
	if (m->is_efi)
		put(&w, "menuentry \"UEFI firmware settings\" {\n    fwsetup\n}\n");
	return finish(&w);
}
