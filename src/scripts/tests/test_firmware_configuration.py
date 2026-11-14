import ast
import configparser
import json
import os
import re
import runpy
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
        self.assertIn("PIO_ENV: ${{ matrix.hardware-variant }}_release", workflow)
        self.assertNotIn("behavior-variant", workflow)
        self.assertIn("env:embedded_tests", config)
        self.assertIn("env:TestUtil", config)

    def test_bench_console_uses_an_opt_in_environment(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read([ROOT / "platformio.ini", *sorted((ROOT / "src/variants").glob("*/platformio.ini"))])
        bench_flag = "LEAF_USB_BENCH_CONSOLE"
        bench = config["env:leaf_3_2_5_bench"]
        self.assertEqual(bench["extends"], "env:leaf_3_2_5_release")
        self.assertIn("${env:leaf_3_2_5_release.build_flags}", bench["build_flags"])
        self.assertIn(f"-D {bench_flag}=1", bench["build_flags"])
        for name in config.sections():
            if not name.endswith("_bench"):
                self.assertNotIn(bench_flag, config[name].get("build_flags", ""))

    def test_bench_artifact_has_a_distinct_filename(self):
        class Environment(dict):
            def subst(self, value):
                assert value == "$BUILD_DIR"
                return self["BUILD_DIR"]

            def AddPostAction(self, target, action):
                assert target == "$BUILD_DIR/${PROGNAME}.bin"
                self.post_action = action

        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            env = Environment(PROGNAME="firmware", BUILD_DIR=directory)
            runpy.run_path(ROOT / "src/scripts/rename_firmware.py",
                           init_globals={"env": env, "Import": lambda _: None})

            for variant, prefix in (("release", "firmware"), ("bench", "bench-firmware")):
                env["PIOENV"] = f"leaf_3_2_5_{variant}"
                contents = variant.encode()
                (build_dir / "firmware.bin").write_bytes(contents)
                env.post_action()
                self.assertEqual((build_dir / f"{prefix}-leaf_3_2_5.bin").read_bytes(), contents)
            self.assertEqual((build_dir / "firmware-leaf_3_2_5.bin").read_bytes(), b"release")

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
