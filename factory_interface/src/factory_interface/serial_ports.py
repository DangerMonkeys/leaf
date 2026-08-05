from serial.tools import list_ports

from factory_interface.settings import load_settings


def available_serial_ports() -> list[dict]:
    ignored_ports = set(load_settings().ignored_serial_ports)
    ports = []
    for port in sorted(list_ports.comports(), key=lambda item: item.device.lower()):
        ports.append(
            {
                "device": port.device,
                "description": port.description or "Serial port",
                "hwid": port.hwid or "",
                "ignored": port.device in ignored_ports,
            }
        )
    return ports


def eligible_serial_ports() -> list[str]:
    return [
        port["device"] for port in available_serial_ports() if not port["ignored"]
    ]
