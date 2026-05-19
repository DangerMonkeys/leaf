from datetime import datetime

from sqlmodel import Field, SQLModel


class File(SQLModel, table=True):
    __tablename__ = "files"

    id: int | None = Field(default=None, primary_key=True)
    name: str
    source: str
    size: int
    sha256: str
    modified_at: datetime


class ConfigurationEvent(SQLModel, table=True):
    __tablename__ = "configuration_event"

    id: int | None = Field(default=None, primary_key=True)
    mac_address: str
    operator: str
    configured_at: datetime
    configuration_action: str
    machine: str
    repo_state: str
    fanet_id: int | None = Field(default=None, ge=0, le=0xFFFFFF)
    firmware: int | None = Field(default=None, foreign_key="files.id")
    bootloader: int | None = Field(default=None, foreign_key="files.id")
    partitions: int | None = Field(default=None, foreign_key="files.id")
    notes: str | None = None
    test_results: str | None = None
