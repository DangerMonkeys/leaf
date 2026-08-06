import asyncio
import uuid
from dataclasses import dataclass, field
from datetime import datetime


@dataclass
class PendingSetupRun:
    run_id: str
    operator: str
    notes: str
    preflight: dict
    serial_ports: list[str]
    status: str = "running"
    details: str = "Preparing to flash firmware."
    created_at: datetime = field(default_factory=datetime.now)
    updated_at: datetime = field(default_factory=datetime.now)
    monitor_task: asyncio.Task | None = None

    @property
    def label(self) -> str:
        if len(self.serial_ports) == 1:
            return f"Unidentified Leaf — {self.serial_ports[0]}"
        if self.serial_ports:
            return "Unidentified Leaf — serial port search"
        return "Unidentified Leaf"

    def touch(self) -> None:
        self.updated_at = datetime.now()

    def snapshot(self) -> dict:
        return {
            "kind": "pending",
            "run_id": self.run_id,
            "label": self.label,
            "url": f"/setup?run_id={self.run_id}",
            "operator": self.operator,
            "notes": self.notes,
            "status": self.status,
            "details": self.details,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "serial_ports": self.serial_ports,
        }


pending_setup_runs: dict[str, PendingSetupRun] = {}


def create_pending_setup_run(
    *,
    operator: str,
    notes: str,
    preflight: dict,
    serial_ports: list[str],
) -> PendingSetupRun:
    run = PendingSetupRun(
        run_id=str(uuid.uuid4()),
        operator=operator,
        notes=notes,
        preflight=preflight,
        serial_ports=serial_ports,
    )
    pending_setup_runs[run.run_id] = run
    return run


def get_pending_setup_run(run_id: str) -> PendingSetupRun | None:
    return pending_setup_runs.get(run_id)


def list_pending_setup_runs() -> list[PendingSetupRun]:
    return sorted(
        pending_setup_runs.values(),
        key=lambda run: run.created_at,
        reverse=True,
    )


def latest_running_pending_setup_run() -> PendingSetupRun | None:
    return next(
        (run for run in list_pending_setup_runs() if run.status == "running"),
        None,
    )


def remove_pending_setup_run(run_id: str) -> None:
    pending_setup_runs.pop(run_id, None)
