# Leaf logbook

[View the logbook data model with the ReDoc viewer](https://redocly.github.io/redoc/?url=https://raw.githubusercontent.com/DangerMonkeys/leaf/refs/heads/main/src/vario/logbook/logbook.yaml)

## Bus log format

The bus log is mostly human-readable as plain text. Each line contains a message logged from the message bus. The first character indicates the type of message and then the format of the data on the line depends on that message type. The message types and formats can be found in the `on_receive` methods in [bus.cpp](./bus.cpp).
