import ast
import configparser
import json
import os
import re
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
FIELD_VARIANTS = {f"leaf_3_2_{version}" for version in (3, 5, 6, 7, 8)}
LAB_VARIANT = "leaf_3_2_2"


class FirmwareConfigurationTests(unittest.TestCase):
    def test_release_environments_and_ci_coverage(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read([ROOT / "platformio.ini", *sorted((ROOT / "src/variants").glob("*/platformio.ini"))])
        self.assertFalse(any(name.endswith("_dev") for name in config.sections()))
        for variant in FIELD_VARIANTS | {LAB_VARIANT}:
            self.assertEqual(config[f"env:{variant}_release"]["extends"], "env:_release")
        workflow = (ROOT / ".github/workflows/build.yaml").read_text()
        self.assertEqual(set(re.findall(r"^\s+- (leaf_\d_\d_\d)\s*$", workflow, re.M)), FIELD_VARIANTS)
        self.assertNotIn("behavior-variant", workflow)
        self.assertIn("env:embedded_tests", config)
        self.assertIn("env:TestUtil", config)

    def test_ota_metadata_only_announces_published_variants(self):
        # Load the pure generator without running PlatformIO's SCons entry point.
        tree = ast.parse((ROOT / "src/scripts/versioning.py").read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                        and node.name == "make_version_files")
        namespace = {"os": os, "json": json}
        exec(compile(ast.Module(body=[function], type_ignores=[]), "versioning.py", "exec"), namespace)

        with tempfile.TemporaryDirectory() as directory:
            class Environment(dict):
                def subst(self, value):
                    assert value == "$BUILD_DIR"
                    return directory

            namespace["make_version_files"](Environment(PROJECT_DIR=str(ROOT)), "0.2.4")
            versions = json.loads((Path(directory) / "latest_versions.json").read_text())
            self.assertEqual(set(versions["latest_tag_versions"]), FIELD_VARIANTS)
            self.assertEqual(set(versions["latest_tag_versions"].values()), {"0.2.4"})
            self.assertEqual((Path(directory) / "leaf.version").read_text(), "0.2.4")


if __name__ == "__main__":
    unittest.main()
