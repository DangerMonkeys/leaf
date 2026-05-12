import json
import os
from pathlib import Path

from implicitdict import ImplicitDict


SETTINGS_PATH = Path(__file__).resolve().parent / "settings.json"
PROJECT_ROOT = Path(__file__).resolve().parents[3]


class FactoryInterfaceSettings(ImplicitDict):
  esptool_path: str | None = None


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
  if SETTINGS_PATH.exists():
    with open(SETTINGS_PATH, "r") as f:
      settings = ImplicitDict.parse(json.load(f), FactoryInterfaceSettings)
  else:
    settings = FactoryInterfaceSettings()
  
  if settings.esptool_path is None or not Path(settings.esptool_path).exists():
    found_path = find_esptool_path()
    settings.esptool_path = str(found_path) if found_path is not None else None
    save_settings(settings)

  return settings


def save_settings(settings: FactoryInterfaceSettings) -> None:
  with open(SETTINGS_PATH, "w") as f:
    json.dump(settings, f, indent=2)
