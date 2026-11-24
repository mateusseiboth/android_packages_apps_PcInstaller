#include "grubcfg.h"

#include <stdio.h>

/* androidboot.selinux=permissive until the vendor policy is complete. */
#define BASE_CMDLINE                                                                                \
	"androidboot.hardware=pc androidboot.selinux=permissive firmware_class.path=/vendor/firmware " \
	"console=tty0 loglevel=4"

static int is_always(const struct grub_params *p) { (void)p; return 1; }
static int has_previous(const struct grub_params *p) { return p->has_previous; }

static const struct {
	const char *title;
	const char *extra;
	int (*is_wanted)(const struct grub_params *);
} ENTRIES[] = {
	{ "LineageOS", "quiet", is_always },
	{ "LineageOS (safe graphics)", "nomodeset", is_always },
	{ "LineageOS (previous system)", "pc.previous=1 quiet", has_previous },
};

void build_kernel_cmdline(char *out, size_t cap, const char *folder, const char *extra)
{
	snprintf(out, cap, "%s pc.src=%s%s%s", BASE_CMDLINE, folder, *extra ? " " : "", extra);
}

void build_id_file(char *out, size_t cap, const char *folder, const char *id)
{
	snprintf(out, cap, "%s/install-%s.id", folder, id);
}

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

static void put_entry(struct writer *w, const struct grub_params *p, const char *title, const char *extra)
{
	char args[256], cmdline[1024], text[1536];
	snprintf(args, sizeof args, "pc.id=%s %s", p->id, extra);
	build_kernel_cmdline(cmdline, sizeof cmdline, p->folder, args);
	snprintf(text, sizeof text, "menuentry \"%s\" {\n    linux /%s/kernel %s\n    initrd /%s/initrd.img\n}\n\n",
		 title, p->folder, cmdline, p->folder);
	put(w, text);
}

int build_grub_cfg(char *out, size_t cap, const struct grub_params *p)
{
	struct writer w = { out, cap, 0, 0 };
	char id_file[256], search[384];
	build_id_file(id_file, sizeof id_file, p->folder, p->id);
	snprintf(search, sizeof search, "search --no-floppy --set=root --file /%s\n\n", id_file);
	put(&w, "# Written by pc-installd; rewritten on every install or update.\n"
		"set timeout=5\nset default=0\n");
	put(&w, search);
	for (size_t i = 0; i < sizeof ENTRIES / sizeof *ENTRIES; i++)
		if (ENTRIES[i].is_wanted(p))
			put_entry(&w, p, ENTRIES[i].title, ENTRIES[i].extra);
	return w.overflow ? -1 : 0;
}
