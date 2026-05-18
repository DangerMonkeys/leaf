import json
import os
from pathlib import Path

from implicitdict import ImplicitDict


SETTINGS_PATH = Path(__file__).resolve().parent / "settings.json"
PROJECT_ROOT = Path(__file__).resolve().parents[3]
REQUIRED_FIRMWARE_FILES = ("bootloader.bin", "partitions.bin", "firmware.bin")


class FactoryInterfaceSettings(ImplicitDict):
  esptool_path: str | None = None
  firmware_path: str | None = None
  setup_notes: str = ""


def find_firmware_paths() -> list[Path]:
  build_root = PROJECT_ROOT / ".pio" / "build"
  if not build_root.exists():
    return []

  firmware_paths = []
  for path in sorted(build_root.iterdir()):
    if not path.is_dir():
      continue
    if all((path / filename).exists() for filename in REQUIRED_FIRMWARE_FILES):
      firmware_paths.append(path)

  return firmware_paths


def is_valid_firmware_path(path: str | None) -> bool:
  if path is None:
    return False

  try:
    selected_path = Path(path).resolve()
  except OSError:
    return False

  return any(selected_path == firmware_path.resolve() for firmware_path in find_firmware_paths())


def find_esptool_path() -> Path | None:
  candidate_roots = []
  if os.environ.get("PLATFORMIO_CORE_DIR"):
    candidate_roots.append(Path(os.environ["PLATFORMIO_CORE_DIR"]))

  candidate_roots.extend([
    Path.home() / ".platformio",
    PROJECT_ROOT / ".platformio",
    PROJECT_ROOT / ".pio",
  ])

  direct_candidates = [
    root / "packages" / "tool-esptoolpy" / "esptool.py"
    for root in candidate_roots
  ]

  for candidate in direct_candidates:
    if candidate.exists():
      return candidate

  for root in candidate_roots:
    if not root.exists():
      continue

    for search_root in [
      root / "packages" / "tool-esptoolpy",
      root / "packages",
      root / "penv",
    ]:
      if not search_root.exists():
        continue

      matches = sorted(search_root.rglob("esptool.py"))
      if matches:
        return matches[0]

  return None


def load_settings() -> FactoryInterfaceSettings:
  settings_data = {}
  if SETTINGS_PATH.exists():
    with open(SETTINGS_PATH, "r") as f:
      settings_data = json.load(f)
      settings = ImplicitDict.parse(settings_data, FactoryInterfaceSettings)
  else:
    settings = FactoryInterfaceSettings()
  
  settings_changed = False

  if "setup_notes" not in settings_data:
    settings.setup_notes = ""
    settings_changed = True

  if not isinstance(settings.setup_notes, str):
    settings.setup_notes = ""
    settings_changed = True

  if settings.esptool_path is None or not Path(settings.esptool_path).exists():
    found_path = find_esptool_path()
    settings.esptool_path = str(found_path) if found_path is not None else None
    settings_changed = True

  if not is_valid_firmware_path(settings.firmware_path):
    firmware_paths = find_firmware_paths()
    settings.firmware_path = str(firmware_paths[0]) if firmware_paths else None
    settings_changed = True

  if settings_changed:
    save_settings(settings)

  return settings


def save_settings(settings: FactoryInterfaceSettings) -> None:
  with open(SETTINGS_PATH, "w") as f:
    json.dump(settings, f, indent=2)
