import hashlib
import json
import os
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from implicitdict import ImplicitDict


SETTINGS_PATH = Path(__file__).resolve().parent / "settings.json"
PROJECT_ROOT = Path(__file__).resolve().parents[3]
GITHUB_CACHE_PATH = PROJECT_ROOT / "factory_interface" / "github_cache"
GITHUB_RELEASES_API_URL = "https://api.github.com/repos/DangerMonkeys/leaf/releases"
LOCAL_APPLICATION_FIRMWARE_FILES = ("firmware.bin",)
NON_APPLICATION_FIRMWARE_FILES = ("bootloader.bin", "partitions.bin")
REQUIRED_FIRMWARE_FILES = NON_APPLICATION_FIRMWARE_FILES + LOCAL_APPLICATION_FIRMWARE_FILES
RELEASE_FIRMWARE_PREFIX = "firmware-"
RELEASE_FIRMWARE_SUFFIX = ".bin"
LOCAL_SOURCE_PREFIX = "local:"
RELEASE_SOURCE_PREFIX = "release:"


class FactoryInterfaceSettings(ImplicitDict):
  esptool_path: str | None = None
  firmware_path: str | None = None
  application_firmware_source: str | None = None
  non_application_firmware_path: str | None = None
  github_releases: list[dict] = []
  setup_notes: str = ""
  force_format_sd_card_during_commissioning: bool = True


def find_build_paths(required_files: tuple[str, ...]) -> list[Path]:
  build_root = PROJECT_ROOT / ".pio" / "build"
  if not build_root.exists():
    return []

  build_paths = []
  for path in sorted(build_root.iterdir()):
    if not path.is_dir():
      continue
    if all((path / filename).exists() for filename in required_files):
      build_paths.append(path)

  return build_paths


def find_firmware_paths() -> list[Path]:
  return find_build_paths(REQUIRED_FIRMWARE_FILES)


def find_application_firmware_paths() -> list[Path]:
  return find_build_paths(LOCAL_APPLICATION_FIRMWARE_FILES)


def find_non_application_binary_paths() -> list[Path]:
  return find_build_paths(NON_APPLICATION_FIRMWARE_FILES)


def local_application_source(path: str | Path) -> str:
  return LOCAL_SOURCE_PREFIX + str(path)


def release_application_source(tag_name: str, asset_name: str) -> str:
  return RELEASE_SOURCE_PREFIX + tag_name + "/" + asset_name


def parse_application_source(source: str | None) -> tuple[str, str | None, str | None]:
  if not source:
    return "", None, None
  if source.startswith(LOCAL_SOURCE_PREFIX):
    return "local", source.removeprefix(LOCAL_SOURCE_PREFIX), None
  if source.startswith(RELEASE_SOURCE_PREFIX):
    release_spec = source.removeprefix(RELEASE_SOURCE_PREFIX)
    tag_name, separator, asset_name = release_spec.partition("/")
    if separator and tag_name and asset_name:
      return "release", tag_name, asset_name
  return "", None, None


def release_firmware_name(asset_name: str) -> str:
  if asset_name.startswith(RELEASE_FIRMWARE_PREFIX) and asset_name.endswith(RELEASE_FIRMWARE_SUFFIX):
    return asset_name[len(RELEASE_FIRMWARE_PREFIX):-len(RELEASE_FIRMWARE_SUFFIX)]
  return asset_name


def is_release_firmware_asset(asset_name: str) -> bool:
  return (
    asset_name.startswith(RELEASE_FIRMWARE_PREFIX)
    and asset_name.endswith(RELEASE_FIRMWARE_SUFFIX)
  )


def is_valid_firmware_path(path: str | None) -> bool:
  return is_valid_non_application_binary_path(path)


def is_valid_path_for_builds(path: str | None, build_paths: list[Path]) -> bool:
  if path is None:
    return False

  try:
    selected_path = Path(path).resolve()
  except OSError:
    return False

  return any(selected_path == build_path.resolve() for build_path in build_paths)


def is_valid_local_application_firmware_path(path: str | None) -> bool:
  return is_valid_path_for_builds(path, find_application_firmware_paths())


def is_valid_non_application_binary_path(path: str | None) -> bool:
  return is_valid_path_for_builds(path, find_non_application_binary_paths())


def iter_cached_release_firmware_assets(settings: FactoryInterfaceSettings):
  releases = settings.github_releases if isinstance(settings.github_releases, list) else []
  for release in releases:
    if not isinstance(release, dict):
      continue
    tag_name = str(release.get("tag_name", "")).strip()
    if not tag_name:
      continue
    assets = release.get("assets", [])
    if not isinstance(assets, list):
      continue
    for asset in assets:
      if not isinstance(asset, dict):
        continue
      asset_name = str(asset.get("name", "")).strip()
      download_url = str(asset.get("browser_download_url", "")).strip()
      if is_release_firmware_asset(asset_name) and download_url:
        yield release, asset


def find_cached_release_firmware_asset(
  settings: FactoryInterfaceSettings,
  tag_name: str,
  asset_name: str,
) -> dict | None:
  for release, asset in iter_cached_release_firmware_assets(settings):
    if release.get("tag_name") == tag_name and asset.get("name") == asset_name:
      return asset
  return None


def is_valid_application_firmware_source(
  source: str | None,
  settings: FactoryInterfaceSettings,
) -> bool:
  source_type, first_part, second_part = parse_application_source(source)
  if source_type == "local":
    return is_valid_local_application_firmware_path(first_part)
  if source_type == "release" and first_part and second_part:
    return find_cached_release_firmware_asset(settings, first_part, second_part) is not None
  return False


def application_firmware_options(settings: FactoryInterfaceSettings) -> list[dict]:
  options = [
    {
      "label": path.name,
      "source": local_application_source(path),
      "details": str(path),
    }
    for path in find_application_firmware_paths()
  ]
  for release, asset in iter_cached_release_firmware_assets(settings):
    tag_name = release["tag_name"]
    asset_name = asset["name"]
    firmware_name = release_firmware_name(asset_name)
    options.append({
      "label": f"release/{tag_name}/{firmware_name}",
      "source": release_application_source(tag_name, asset_name),
      "details": asset_name,
    })
  return options


def non_application_binary_options() -> list[dict]:
  return [
    {
      "label": path.name,
      "path": str(path),
      "details": str(path),
    }
    for path in find_non_application_binary_paths()
  ]


def describe_application_firmware_source(
  source: str | None,
  settings: FactoryInterfaceSettings,
) -> str:
  source_type, first_part, second_part = parse_application_source(source)
  if source_type == "local" and first_part:
    return "local/" + Path(first_part).name
  if source_type == "release" and first_part and second_part:
    return f"release/{first_part}/{release_firmware_name(second_part)}"
  return "No firmware selected"


def describe_non_application_binary_path(path: str | None) -> str:
  if path:
    return "local/" + Path(path).name
  return "No non-application binaries selected"


def refresh_github_releases(settings: FactoryInterfaceSettings) -> None:
  releases = []
  page = 1
  per_page = 100
  try:
    while True:
      request = Request(
        f"{GITHUB_RELEASES_API_URL}?per_page={per_page}&page={page}",
        headers={
          "Accept": "application/vnd.github+json",
          "User-Agent": "factory-interface",
        },
      )
      with urlopen(request, timeout=20) as response:
        page_releases = json.load(response)
      if not isinstance(page_releases, list):
        raise RuntimeError("GitHub releases response was not a list.")
      releases.extend(page_releases)
      if len(page_releases) < per_page:
        break
      page += 1
  except (HTTPError, URLError, TimeoutError) as exc:
    raise RuntimeError(f"Could not read GitHub releases: {exc}") from exc

  cached_releases = []
  for release in releases:
    if not isinstance(release, dict):
      continue
    assets = []
    for asset in release.get("assets", []):
      if not isinstance(asset, dict):
        continue
      asset_name = str(asset.get("name", "")).strip()
      if not is_release_firmware_asset(asset_name):
        continue
      assets.append({
        "name": asset_name,
        "browser_download_url": asset.get("browser_download_url", ""),
        "size": asset.get("size"),
        # GitHub returns "sha256:<hex>" (may be absent on older assets). Stored so a
        # cached file can be validated and re-downloaded if a release asset with the
        # same tag/name is later re-uploaded with different contents.
        "digest": asset.get("digest"),
      })
    if assets:
      cached_releases.append({
        "tag_name": release.get("tag_name", ""),
        "name": release.get("name", ""),
        "published_at": release.get("published_at", ""),
        "created_at": release.get("created_at", ""),
        "assets": sorted(assets, key=lambda asset: asset["name"]),
      })

  cached_releases.sort(
    key=lambda release: release.get("published_at") or release.get("created_at") or "",
    reverse=True,
  )
  settings.github_releases = cached_releases
  save_settings(settings)


def cached_release_asset_path(tag_name: str, asset_name: str) -> Path:
  safe_tag_name = tag_name.replace("/", "_").replace("\\", "_")
  safe_asset_name = asset_name.replace("/", "_").replace("\\", "_")
  return GITHUB_CACHE_PATH / safe_tag_name / safe_asset_name


def cached_asset_is_current(cache_path: Path, asset: dict) -> bool:
  """Whether the on-disk cached file still matches the release asset metadata.

  Release assets can be re-uploaded under the same tag/name with different
  contents (e.g. a rebuilt firmware). The cache path is keyed only by tag/name,
  so without this check a stale binary would be served forever. Validate against
  the GitHub-provided sha256 digest when available, otherwise fall back to the
  byte size. If no metadata is available to compare, assume current (preserve the
  prior behavior rather than force needless re-downloads).
  """
  if not cache_path.exists():
    return False

  digest = asset.get("digest")
  if isinstance(digest, str) and digest.startswith("sha256:"):
    expected = digest.split(":", 1)[1].strip().lower()
    if expected:
      hasher = hashlib.sha256()
      with open(cache_path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
          hasher.update(chunk)
      return hasher.hexdigest() == expected

  expected_size = asset.get("size")
  if isinstance(expected_size, int):
    return cache_path.stat().st_size == expected_size

  return True


def acquire_release_firmware_asset(
  settings: FactoryInterfaceSettings,
  tag_name: str,
  asset_name: str,
) -> Path:
  asset = find_cached_release_firmware_asset(settings, tag_name, asset_name)
  if asset is None:
    raise RuntimeError(f"Release firmware is not cached in settings: {tag_name}/{asset_name}")

  cache_path = cached_release_asset_path(tag_name, asset_name)
  if cached_asset_is_current(cache_path, asset):
    return cache_path

  download_url = asset.get("browser_download_url")
  if not download_url:
    raise RuntimeError(f"Release firmware has no download URL: {tag_name}/{asset_name}")

  cache_path.parent.mkdir(parents=True, exist_ok=True)
  request = Request(
    download_url,
    headers={
      "Accept": "application/octet-stream",
      "User-Agent": "factory-interface",
    },
  )
  try:
    with urlopen(request, timeout=60) as response:
      with open(cache_path, "wb") as f:
        while True:
          chunk = response.read(1024 * 1024)
          if not chunk:
            break
          f.write(chunk)
  except (HTTPError, URLError, TimeoutError) as exc:
    if cache_path.exists():
      cache_path.unlink()
    raise RuntimeError(f"Could not download release firmware: {exc}") from exc

  return cache_path


def resolve_application_firmware_file(
  settings: FactoryInterfaceSettings,
) -> Path:
  source_type, first_part, second_part = parse_application_source(settings.application_firmware_source)
  if source_type == "local" and first_part:
    firmware_file = Path(first_part) / "firmware.bin"
    if firmware_file.exists():
      return firmware_file
    raise RuntimeError(f"Local firmware.bin does not exist: {firmware_file}")
  if source_type == "release" and first_part and second_part:
    return acquire_release_firmware_asset(settings, first_part, second_part)
  raise RuntimeError("Application firmware is not configured.")


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

  if not isinstance(settings.force_format_sd_card_during_commissioning, bool):
    settings.force_format_sd_card_during_commissioning = True
    settings_changed = True

  if not isinstance(settings.github_releases, list):
    settings.github_releases = []
    settings_changed = True

  legacy_firmware_path = settings_data.get("firmware_path")
  if settings.application_firmware_source is None and isinstance(legacy_firmware_path, str):
    settings.application_firmware_source = local_application_source(legacy_firmware_path)
    settings_changed = True

  if settings.non_application_firmware_path is None and isinstance(legacy_firmware_path, str):
    settings.non_application_firmware_path = legacy_firmware_path
    settings_changed = True

  if settings.esptool_path is None or not Path(settings.esptool_path).exists():
    found_path = find_esptool_path()
    settings.esptool_path = str(found_path) if found_path is not None else None
    settings_changed = True

  if not is_valid_application_firmware_source(settings.application_firmware_source, settings):
    firmware_paths = find_application_firmware_paths()
    settings.application_firmware_source = (
      local_application_source(firmware_paths[0]) if firmware_paths else None
    )
    settings_changed = True

  if not is_valid_non_application_binary_path(settings.non_application_firmware_path):
    binary_paths = find_non_application_binary_paths()
    settings.non_application_firmware_path = str(binary_paths[0]) if binary_paths else None
    settings_changed = True

  settings.firmware_path = settings.non_application_firmware_path

  if settings_changed:
    save_settings(settings)

  return settings


def save_settings(settings: FactoryInterfaceSettings) -> None:
  with open(SETTINGS_PATH, "w") as f:
    json.dump(settings, f, indent=2)
