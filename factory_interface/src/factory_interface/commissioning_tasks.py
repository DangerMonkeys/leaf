import asyncio
import configparser
import json
import shlex
import sys
from dataclasses import dataclass, field
from pathlib import Path
from subprocess import list2cmdline

from factory_interface.network_discovery import (
    start_find_device,
    start_preflash_monitor,
    stop_preflash_monitor,
)
from factory_interface.settings import load_settings


PROJECT_ROOT = Path(__file__).resolve().parents[3]


@dataclass
class FlashFirmwareTask:
    status: str = "idle"
    output: str = ""
    return_code: int | None = None
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)

    def snapshot(self) -> dict:
        return {
            "status": self.status,
            "output": self.output,
            "return_code": self.return_code,
        }


flash_tasks: dict[str, FlashFirmwareTask] = {}


class FlashCommandError(RuntimeError):
    pass


def load_platformio_configs() -> configparser.ConfigParser:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str

    config_paths = [PROJECT_ROOT / "platformio.ini"]
    config_paths.extend(sorted((PROJECT_ROOT / "src" / "variants").glob("*/platformio.ini")))
    config.read([str(path) for path in config_paths if path.exists()])
    return config


def normalize_frequency(value: str) -> str:
    clean_value = value.strip().removesuffix("L")
    if clean_value.isdigit():
        frequency = int(clean_value)
        if frequency >= 1000000:
            return f"{frequency // 1000000}m"
        if frequency >= 1000:
            return f"{frequency // 1000}k"
    return value.strip()


def platformio_upload_flash_mode(flash_mode: str) -> str:
    if flash_mode in ("qio", "qout"):
        return "dio"
    return flash_mode


def platformio_env_option(
    config: configparser.ConfigParser,
    env_name: str,
    option: str,
    *,
    visited: set[str] | None = None,
) -> str | None:
    visited = visited or set()
    section = f"env:{env_name}"
    if section in visited or not config.has_section(section):
        return None

    visited.add(section)

    if config.has_option(section, option):
        return config.get(section, option).strip()

    if not config.has_option(section, "extends"):
        return None

    parent_sections = [
        parent.strip()
        for parent in config.get(section, "extends").splitlines()
        if parent.strip()
    ]
    for parent_section in parent_sections:
        parent_env_name = parent_section.removeprefix("env:")
        value = platformio_env_option(
            config,
            parent_env_name,
            option,
            visited=visited,
        )
        if value is not None:
            return value

    return None


def load_platformio_board_manifest(board_name: str) -> dict:
    board_paths = [
        Path.home()
        / ".platformio"
        / "platforms"
        / "espressif32"
        / "boards"
        / f"{board_name}.json",
    ]
    board_paths.extend(
        sorted(
            (Path.home() / ".platformio" / "platforms").glob(
                f"espressif32*/boards/{board_name}.json"
            )
        )
    )

    for board_path in board_paths:
        if board_path.exists():
            with open(board_path, "r") as f:
                return json.load(f)

    return {}


def get_flash_task(serial_number: str) -> FlashFirmwareTask:
    if serial_number not in flash_tasks:
        flash_tasks[serial_number] = FlashFirmwareTask()
    return flash_tasks[serial_number]


def format_command(command: list[str]) -> str:
    if sys.platform.startswith("win"):
        return list2cmdline(command)
    return shlex.join(command)


def find_boot_app0_path(firmware_path: Path) -> Path | None:
    idedata_path = firmware_path / "idedata.json"
    if idedata_path.exists():
        with open(idedata_path, "r") as f:
            idedata = json.load(f)

        for image in idedata.get("extra", {}).get("flash_images", []):
            if image.get("offset") == "0xe000":
                candidate = Path(image["path"])
                if candidate.exists():
                    return candidate

    candidates = [
        Path.home()
        / ".platformio"
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin",
        firmware_path / "boot_app0.bin",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    return None


def build_flash_firmware_command() -> tuple[list[str], Path]:
    settings = load_settings()
    if settings.esptool_path is None:
        raise FlashCommandError("esptool.py path is not configured.")
    if settings.firmware_path is None:
        raise FlashCommandError("Firmware path is not configured.")

    esptool_path = Path(settings.esptool_path)
    firmware_path = Path(settings.firmware_path)

    if not esptool_path.exists():
        raise FlashCommandError(f"esptool.py does not exist: {esptool_path}")
    if not firmware_path.exists():
        raise FlashCommandError(f"Firmware path does not exist: {firmware_path}")

    required_files = {
        "bootloader.bin": firmware_path / "bootloader.bin",
        "partitions.bin": firmware_path / "partitions.bin",
        "firmware.bin": firmware_path / "firmware.bin",
    }
    missing_files = [
        filename for filename, path in required_files.items() if not path.exists()
    ]
    if missing_files:
        raise FlashCommandError(
            "Firmware path is missing required files: " + ", ".join(missing_files)
        )

    platformio_config = load_platformio_configs()
    board_name = platformio_env_option(platformio_config, firmware_path.name, "board")
    board_manifest = load_platformio_board_manifest(board_name) if board_name else {}
    flash_mode = platformio_env_option(
        platformio_config,
        firmware_path.name,
        "board_build.flash_mode",
    ) or board_manifest.get("build", {}).get("flash_mode")
    upload_speed = platformio_env_option(
        platformio_config,
        firmware_path.name,
        "upload_speed",
    ) or str(board_manifest.get("upload", {}).get("speed", 460800))
    flash_freq = platformio_env_option(
        platformio_config,
        firmware_path.name,
        "board_build.f_image",
    ) or platformio_env_option(
        platformio_config,
        firmware_path.name,
        "board_build.f_flash",
    ) or board_manifest.get("build", {}).get("f_flash")
    if flash_mode is None:
        raise FlashCommandError(
            f"Could not determine flash mode for PlatformIO env {firmware_path.name}."
        )
    if flash_freq is None:
        raise FlashCommandError(
            f"Could not determine flash frequency for PlatformIO env {firmware_path.name}."
        )

    upload_flash_mode = platformio_upload_flash_mode(flash_mode)
    upload_flash_freq = normalize_frequency(flash_freq)

    command = [
        sys.executable,
        str(esptool_path),
        "--chip",
        "esp32s3",
        "--baud",
        str(upload_speed),
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "write_flash",
        "-z",
        "--flash_mode",
        upload_flash_mode,
        "--flash_freq",
        upload_flash_freq,
        "--flash_size",
        "detect",
        "0x0000",
        str(required_files["bootloader.bin"]),
        "0x8000",
        str(required_files["partitions.bin"]),
    ]

    boot_app0_path = find_boot_app0_path(firmware_path)
    if boot_app0_path is not None:
        command.extend(["0xe000", str(boot_app0_path)])

    command.extend([
        "0x10000",
        str(required_files["firmware.bin"]),
    ])

    return command, firmware_path


async def run_flash_firmware(serial_number: str) -> None:
    task = get_flash_task(serial_number)

    async with task.lock:
        task.status = "running"
        task.output = ""
        task.return_code = None

        try:
            start_preflash_monitor(serial_number)
            command, firmware_path = build_flash_firmware_command()
            task.output += f"$ {format_command(command)}\n"
            process = await asyncio.create_subprocess_exec(
                *command,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                cwd=firmware_path,
            )

            if process.stdout is not None:
                while True:
                    chunk = await process.stdout.read(1024)
                    if not chunk:
                        break
                    task.output += chunk.decode(errors="replace")

            task.return_code = await process.wait()
            if task.return_code == 0:
                task.status = "success"
                start_find_device(serial_number)
            else:
                task.status = "failure"
        except Exception as exc:
            task.output += f"\n{type(exc).__name__}: {exc}\n"
            task.return_code = -1
            task.status = "failure"
        finally:
            stop_preflash_monitor(serial_number)


def start_flash_firmware(serial_number: str) -> FlashFirmwareTask:
    task = get_flash_task(serial_number)
    if task.status == "running":
        return task

    task.status = "running"
    task.output = "Starting flash firmware task...\n"
    task.return_code = None
    asyncio.create_task(run_flash_firmware(serial_number))
    return task
