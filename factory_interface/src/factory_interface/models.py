from datetime import datetime

from sqlmodel import Field, SQLModel


class Device(SQLModel, table=True):
    serial_number: str = Field(primary_key=True)
    fanet_id: int | None = Field(default=None, ge=0, le=0xFFFFFFFF)
    automated_test_version: str | None = None
    automated_test_passed_at: datetime | None = None
    manual_test_version: str | None = None
    manual_test_passed_at: datetime | None = None
    mac_address: str | None = None
    factory_configured_at: datetime | None = None
