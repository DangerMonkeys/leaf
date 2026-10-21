import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from factory_interface import settings


class FirmwareBuildSelectionTests(unittest.TestCase):
  def test_only_release_builds_are_selectable(self):
    with tempfile.TemporaryDirectory() as directory:
      root = Path(directory)
      build_root = root / ".pio" / "build"
      for environment in ("leaf_3_2_6_dev", "leaf_3_2_6_release", "embedded_tests"):
        build = build_root / environment
        build.mkdir(parents=True)
        for filename in settings.REQUIRED_FIRMWARE_FILES:
          (build / filename).touch()

      with patch.object(settings, "PROJECT_ROOT", root):
        release = build_root / "leaf_3_2_6_release"
        self.assertEqual(settings.find_application_firmware_paths(), [release])
        self.assertEqual(settings.find_non_application_binary_paths(), [release])
        old_dev = str(build_root / "leaf_3_2_6_dev")
        self.assertFalse(settings.is_valid_local_application_firmware_path(old_dev))
        self.assertFalse(settings.is_valid_non_application_binary_path(old_dev))
        self.assertTrue(settings.is_valid_local_application_firmware_path(str(release)))


if __name__ == "__main__":
  unittest.main()
