"""Create initial configuration tables

Revision ID: 0001_create_initial_tables
Revises:
Create Date: 2026-05-11 22:23:00

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
import sqlmodel

revision: str = "0001_create_initial_tables"
down_revision: Union[str, None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "files",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("name", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("source", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("size", sa.Integer(), nullable=False),
        sa.Column("sha256", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("modified_at", sa.DateTime(), nullable=False),
        sa.PrimaryKeyConstraint("id"),
    )

    op.create_table(
        "configuration_event",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("mac_address", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("operator", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("configured_at", sa.DateTime(), nullable=False),
        sa.Column("configuration_action", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("machine", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("repo_state", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("fanet_id", sa.Integer(), nullable=True),
        sa.Column("firmware", sa.Integer(), nullable=True),
        sa.Column("bootloader", sa.Integer(), nullable=True),
        sa.Column("partitions", sa.Integer(), nullable=True),
        sa.Column("notes", sqlmodel.sql.sqltypes.AutoString(), nullable=True),
        sa.Column("test_results", sqlmodel.sql.sqltypes.AutoString(), nullable=True),
        sa.CheckConstraint(
            "fanet_id IS NULL OR (fanet_id >= 0 AND fanet_id <= 16777215)",
            name="ck_configuration_event_fanet_id_uint32",
        ),
        sa.ForeignKeyConstraint(["firmware"], ["files.id"]),
        sa.ForeignKeyConstraint(["bootloader"], ["files.id"]),
        sa.ForeignKeyConstraint(["partitions"], ["files.id"]),
        sa.PrimaryKeyConstraint("id"),
    )


def downgrade() -> None:
    op.drop_table("configuration_event")
    op.drop_table("files")
