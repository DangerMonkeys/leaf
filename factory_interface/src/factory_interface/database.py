from pathlib import Path

from sqlmodel import Session, SQLModel, create_engine

DATABASE_URL = f"sqlite:///{Path('leaf_devices.db')}"

engine = create_engine(DATABASE_URL, echo=False)


def create_db_and_tables() -> None:
    SQLModel.metadata.create_all(engine)


def get_session() -> Session:
    with Session(engine) as session:
        yield session
