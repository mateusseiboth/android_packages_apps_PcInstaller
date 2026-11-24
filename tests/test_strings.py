"""UI texts: every string in both languages, the same placeholders, and never an em dash."""
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

RES = Path(__file__).resolve().parents[1] / "res"
LANGUAGES = ("values", "values-pt")


def read_strings(folder):
    return {e.get("name"): e.text or "" for e in ET.parse(RES / folder / "strings.xml").getroot() if e.tag == "string"}


class StringsTest(unittest.TestCase):
    def test_both_languages_have_the_same_strings(self):
        english, portuguese = (read_strings(f) for f in LANGUAGES)
        self.assertEqual(set(english), set(portuguese))

    def test_placeholders_match(self):
        english, portuguese = (read_strings(f) for f in LANGUAGES)
        for name, text in english.items():
            self.assertEqual(sorted(re.findall(r"%\d\$[sd]", text)), sorted(re.findall(r"%\d\$[sd]", portuguese[name])),
                             name)

    def test_no_em_dash(self):
        for folder in LANGUAGES:
            for name, text in read_strings(folder).items():
                self.assertNotIn("—", text, f"{folder}/{name}")

    def test_never_asks_to_turn_secure_boot_off(self):
        # shim and the enrolled key start LineageOS with Secure Boot on.
        english, portuguese = (read_strings(f) for f in LANGUAGES)
        self.assertFalse([n for n, t in english.items() if "turn it off" in t.lower()])
        self.assertFalse([n for n, t in portuguese.items() if "desative-a" in t.lower()])

    def test_the_key_steps_name_mokmanager_options_as_shown_on_screen(self):
        # MokManager is English only; the steps quote its options as they appear.
        for folder in LANGUAGES:
            text = read_strings(folder)["key_queued"]
            for option in ("Enroll MOK", "Continue", "Yes", "Reboot", "%1$s"):
                self.assertIn(option, text, folder)


if __name__ == "__main__":
    unittest.main()
