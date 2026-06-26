# Leaf logbook

[View the per-flight logbook entry schema with the ReDoc viewer](https://redocly.github.io/redoc/?url=https://raw.githubusercontent.com/DangerMonkeys/leaf/refs/heads/main/src/vario/logbook/logbook.yaml)

Each saved flight should have one authoritative JSON file in `/logbook`. Any webserver list or index response should be derived from those files, so the index can be rebuilt if entries are added, edited, or deleted.

## Bus log format

The bus log is mostly human-readable as plain text. Each line contains a message logged from the message bus. The first character indicates the type of message and then the format of the data on the line depends on that message type. The message types and formats can be found in the `on_receive` methods in [bus.cpp](./bus.cpp).
