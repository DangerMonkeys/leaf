import asyncio
import sys
from dataclasses import dataclass, field


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


def get_flash_task(serial_number: str) -> FlashFirmwareTask:
    if serial_number not in flash_tasks:
        flash_tasks[serial_number] = FlashFirmwareTask()
    return flash_tasks[serial_number]


async def run_flash_firmware(serial_number: str) -> None:
    task = get_flash_task(serial_number)

    async with task.lock:
        task.status = "running"
        task.output = ""
        task.return_code = None

        command = ["cmd.exe", "/c", "timeout", "/t", "10"]
        if not sys.platform.startswith("win"):
            command = ["timeout", "10"]

        try:
            task.output += f"$ {' '.join(command)}\n"
            process = await asyncio.create_subprocess_exec(
                *command,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
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
            else:
                task.status = "failure"
        except Exception as exc:
            task.output += f"\n{type(exc).__name__}: {exc}\n"
            task.return_code = -1
            task.status = "failure"


def start_flash_firmware(serial_number: str) -> FlashFirmwareTask:
    task = get_flash_task(serial_number)
    if task.status == "running":
        return task

    task.status = "running"
    task.output = "Starting flash firmware task...\n"
    task.return_code = None
    asyncio.create_task(run_flash_firmware(serial_number))
    return task
