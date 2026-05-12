"""Create device table

Revision ID: 0001_create_device_table
Revises:
Create Date: 2026-05-11 22:23:00

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa
import sqlmodel

revision: str = "0001_create_device_table"
down_revision: Union[str, None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "device",
        sa.Column("serial_number", sqlmodel.sql.sqltypes.AutoString(), nullable=False),
        sa.Column("fanet_id", sa.Integer(), nullable=True),
        sa.Column("automated_test_version", sqlmodel.sql.sqltypes.AutoString(), nullable=True),
        sa.Column("automated_test_passed_at", sa.DateTime(), nullable=True),
        sa.Column("manual_test_version", sqlmodel.sql.sqltypes.AutoString(), nullable=True),
        sa.Column("manual_test_passed_at", sa.DateTime(), nullable=True),
        sa.Column("mac_address", sqlmodel.sql.sqltypes.AutoString(), nullable=True),
        sa.Column("factory_configured_at", sa.DateTime(), nullable=True),
        sa.CheckConstraint("fanet_id IS NULL OR (fanet_id >= 0 AND fanet_id <= 4294967295)", name="ck_device_fanet_id_uint32"),
        sa.PrimaryKeyConstraint("serial_number"),
    )


def downgrade() -> None:
    op.drop_table("device")
