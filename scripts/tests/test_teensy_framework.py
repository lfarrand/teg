import json
import tempfile
import unittest
from pathlib import Path

from scripts.teensy_framework import (
    COPY_DIRNAME,
    ensure_framework_copy,
    framework_copy_dir,
    framework_identity,
    remap_compile_path,
    rewrite_abs_path,
    rewrite_compiler_flags,
    rewrite_path_list,
)


WPROGRAM = '#include "usb_flightsim.h"\n#include "MTP_Teensy.h"\n#include "usb_audio.h"\n'


class TeensyFrameworkTests(unittest.TestCase):
    def package(self, root: Path, version="1.162.0") -> Path:
        src = root / "framework-arduinoteensy"
        (src / "cores" / "teensy4").mkdir(parents=True)
        (src / "package.json").write_text(
            json.dumps({"name": "framework-arduinoteensy", "version": version}),
            encoding="utf-8",
        )
        (src / "cores" / "teensy4" / "WProgram.h").write_text(WPROGRAM, encoding="utf-8")
        (src / "cores" / "teensy4" / "Arduino.h").write_text('#include "WProgram.h"\n', encoding="utf-8")
        return src

    def test_copy_dir_is_under_pio(self):
        path = framework_copy_dir("D:/git/teg")
        self.assertEqual(path.parts[-2:], (".pio", COPY_DIRNAME))

    def test_identity_reads_package_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = self.package(Path(tmp))
            self.assertEqual(framework_identity(src), "framework-arduinoteensy@1.162.0")

    def test_ensure_copy_does_not_write_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = self.package(root)
            dst = root / "copy"
            ensure_framework_copy(src, dst)
            self.assertEqual((src / "cores" / "teensy4" / "WProgram.h").read_text(encoding="utf-8"), WPROGRAM)
            copied = (dst / "cores" / "teensy4" / "WProgram.h").read_text(encoding="utf-8")
            self.assertNotIn("MTP_Teensy.h", copied)
            self.assertIn("do not include MTP here", copied)

    def test_reuse_skips_recopy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = self.package(root)
            dst = root / "copy"
            ensure_framework_copy(src, dst)
            marker = dst / "keep-me"
            marker.write_text("x", encoding="utf-8")
            ensure_framework_copy(src, dst)
            self.assertEqual(marker.read_text(encoding="utf-8"), "x")
            self.assertEqual((src / "cores" / "teensy4" / "WProgram.h").read_text(encoding="utf-8"), WPROGRAM)

    def test_identity_change_recopies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = self.package(root)
            dst = root / "copy"
            ensure_framework_copy(src, dst)
            (dst / "keep-me").write_text("x", encoding="utf-8")
            (src / "package.json").write_text(
                json.dumps({"name": "framework-arduinoteensy", "version": "1.163.0"}),
                encoding="utf-8",
            )
            ensure_framework_copy(src, dst)
            self.assertFalse((dst / "keep-me").is_file())
            self.assertEqual((src / "cores" / "teensy4" / "WProgram.h").read_text(encoding="utf-8"), WPROGRAM)

    def test_rewrite_and_remap(self):
        installed = Path("C:/Users/lee/.platformio/packages/framework-arduinoteensy")
        copy = Path("D:/git/teg/.pio") / COPY_DIRNAME
        core = str(installed / "cores" / "teensy4")
        self.assertEqual(rewrite_abs_path(core, installed, copy), str(copy / "cores" / "teensy4"))
        self.assertEqual(rewrite_abs_path("/other", installed, copy), "/other")
        self.assertEqual(
            rewrite_path_list([core, "/other"], installed, copy),
            [str(copy / "cores" / "teensy4"), "/other"],
        )
        self.assertEqual(
            rewrite_compiler_flags(["-I" + core, "-O2"], installed, copy),
            ["-I" + str(copy / "cores" / "teensy4"), "-O2"],
        )
        mtp = str(installed / "cores" / "teensy4" / "MTP_Teensy.cpp")
        remapped = remap_compile_path(mtp, "D:/git/teg", installed, copy)
        self.assertTrue(remapped.replace("\\", "/").endswith("scripts/mtp_core162/MTP_Teensy.cpp"))
        other = remap_compile_path(str(installed / "cores" / "teensy4" / "delay.c"), "D:/git/teg", installed, copy)
        self.assertTrue(other.replace("\\", "/").endswith("cores/teensy4/delay.c"))
        self.assertIn(COPY_DIRNAME, other.replace("\\", "/"))


if __name__ == "__main__":
    unittest.main()
