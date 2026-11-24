"""The pc-installd binary: its socket loop, and the kernel command line it shares with the live ISO."""
import os
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXE = HERE.parent / "build/pc-installd"
ISO_DIR = HERE.parents[4] / "device/pc/pc_x86_64/iso"


def run(*args):
    return subprocess.run([str(EXE), *args], capture_output=True, text=True, check=True).stdout


class SocketTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        for d in ("sys/class/block", "dev/block", "work"):
            (root / d).mkdir(parents=True)
        (root / "mountinfo").write_text("1 0 8:1 / / rw - ext4 /dev/sda1 rw\n")
        (root / "cmdline").write_text("quiet\n")
        self.path = str(root / "sock")
        self.proc = subprocess.Popen([str(EXE), "--socket", self.path, "--root", str(root)])
        for _ in range(100):
            if os.path.exists(self.path):
                break
            time.sleep(0.02)

    def tearDown(self):
        self.proc.kill()
        self.proc.wait()
        self.tmp.cleanup()

    def talk(self, *lines):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.connect(self.path)
            f = s.makefile("rw")
            replies = []
            for line in lines:
                f.write(line + "\n")
                f.flush()
                while True:
                    reply = f.readline().rstrip("\n")
                    replies.append(reply)
                    if reply.startswith(("OK", "ERR")):
                        break
            return replies

    def test_answers_each_request_on_one_connection(self):
        replies = self.talk("INFO", "LIST", "FORMAT")
        self.assertTrue(replies[0].startswith("INFO version=2 uefi=0 secure_boot=0 live=0"))
        self.assertEqual(replies[1:], ["OK", "OK", "ERR code=bad_request"])

    def test_a_new_connection_after_the_first_one_closes(self):
        self.talk("INFO")
        self.assertEqual(self.talk("CHECK part=sda1 folder=lineage system=image data=image")[-1],
                         "ERR code=source_missing")


class CmdlineTest(unittest.TestCase):
    @unittest.skipUnless((ISO_DIR / "build_iso.py").exists(), "needs the device tree")
    def test_matches_the_live_iso(self):
        sys.path.insert(0, str(ISO_DIR))
        from build_iso import kernel_cmdline
        self.assertEqual(run("--cmdline", "lineage", "quiet").strip(), kernel_cmdline("lineage", "quiet"))
        self.assertEqual(run("--cmdline", "os/lin", "").strip(), kernel_cmdline("os/lin", ""))

    def test_prints_the_grub_entry(self):
        entry = run("--grub-entry", "lineage", "00ff", "Dados", "previous")
        self.assertIn('menuentry "LineageOS (Dados)" --id lineage-00ff', entry)
        self.assertIn("pc.previous=1", entry)
        self.assertIn("--file /lineage/install-00ff.id", entry)


if __name__ == "__main__":
    unittest.main()
