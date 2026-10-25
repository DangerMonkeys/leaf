import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "package_training_firmware", Path(__file__).resolve().parents[1] / "package_training_firmware.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def image(self, hardware, marker=None, version=None):
        environment = "leaf_student_" + hardware + "_training"
        folder = self.root / ".pio" / "build" / environment
        folder.mkdir(parents=True, exist_ok=True)
        image = b"\xe9" + bytes(4096)
        image += (marker or "LEAF_HARDWARE_VARIANT=leaf_student_" + hardware).encode() + b"\0"
        image += (version or "+h" + hardware.replace("_", ".")).encode() + b"\0"
        (folder / "firmware.bin").write_bytes(image)
        return environment

    @patch.object(module.subprocess, "check_output", return_value="a" * 40)
    def test_all_versions_and_manifest(self, _):
        for hardware in ("3_2_5", "3_2_6", "3_2_7"):
            with self.subTest(hardware=hardware):
                environment = self.image(hardware)
                output = self.root / hardware
                binary = module.package(self.root, environment, output)
                self.assertEqual(binary.name,
                    f"firmware-leaf_student_{hardware}-{hardware.replace('_', '.')}-training.bin")
                manifest = json.loads((output / "build-info.json").read_text())
                self.assertEqual(manifest["size_bytes"], binary.stat().st_size)
                self.assertIn(manifest["sha256"], (output / "SHA256SUMS.txt").read_text())
                self.assertEqual(manifest["source_commit"], "a" * 40)

    def test_sd_filename_boundaries(self):
        accepts = module.sd_filename_matches
        self.assertFalse(accepts("firmware-leaf_student_3_2_6.bin", "leaf_3_2_6"))
        self.assertFalse(accepts("firmware-3.2.6.bin", "leaf_3_2_6"))
        name = "firmware-leaf_student_3_2_6-3.2.6-training.bin"
        self.assertTrue(accepts(name, "leaf_3_2_6"))
        self.assertTrue(accepts(name, "leaf_student_3_2_6"))
        self.assertFalse(accepts(name, "leaf_3_2_5"))
        self.assertFalse(accepts(name, "leaf_3_2_7"))

    def test_reject_wrong_hardware_and_version(self):
        environment = self.image("3_2_6", marker="LEAF_HARDWARE_VARIANT=leaf_student_3_2_7")
        with self.assertRaisesRegex(ValueError, "hardware marker"):
            module.package(self.root, environment, self.root / "output")
        environment = self.image("3_2_6", version="+h3.2.7")
        with self.assertRaisesRegex(ValueError, "version"):
            module.package(self.root, environment, self.root / "output")

    def test_reject_instructor_and_invalid_images(self):
        with self.assertRaises(ValueError):
            module.package(self.root, "leaf_instructor_3_2_6_training", self.root / "output")
        environment = self.image("3_2_6")
        source = self.root / ".pio" / "build" / environment / "firmware.bin"
        for data in (b"\xe9", bytes(4096)):
            source.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "size/header"):
                module.package(self.root, environment, self.root / "output")


if __name__ == "__main__":
    unittest.main()
