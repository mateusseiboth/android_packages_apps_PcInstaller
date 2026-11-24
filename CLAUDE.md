# PcInstaller: notes for agents

- Never run `m`/`lunch`/soong here to test: use the host tests (README, "Tests"). `tools/typecheck.sh`
  compiles the Compose UI without a build; keep JVMs at -Xmx1.5g or less.
- TDD: test first (`installd/tests/test_*.c` with `check.h`, `core/tests/*Test.kt`), see it fail, then code.
- The daemon's privileged calls go through `sys_ops`/`sys_env` (`installd/src/sysops.h`); host tests use the
  fake machine in `installd/tests/fakesys.h` (mounts are symlinks to `mnt-<dev>` dirs).
- Tables over if/else: commands (`server.c` HANDLERS), steps (`install.c` STEPS), file system rules
  (`plan.c` FS_RULES), signatures (`fsprobe.c`). Names start with an English verb or prefix.
- UI text: pt-BR in `res/values-pt`, English in `res/values`; what happened and what to do, one or two
  sentences, no em dash, no design explanations (`tests/test_strings.py` checks parity and dashes).
- GRUB menus: every install owns `entries/<id>.cfg` in every menu directory (`bootmenu.c`, `menus.c`); never write
  a whole grub.cfg for one install. Tests run menus through `grub2-script-check`; `tools/bios_boot_test.sh` boots
  the BIOS code in QEMU (128 MiB).
- Protocol changes: update `installd/src/server.c`, `core/.../InstallerClient.kt` and the README table together.
- Commits: no AI attribution lines.
