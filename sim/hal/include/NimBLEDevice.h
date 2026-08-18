// NimBLE stand-in: the emulator has no Bluetooth radio, so comms/ble.cpp is replaced by a sim
// implementation and only these type names are needed to compile comms/ble.h.
#pragma once

#include <stdint.h>

#include "WString.h"

class NimBLEServer;
class NimBLEService;
class NimBLECharacteristic;
class NimBLEAdvertising;
class NimBLEClient;
class NimBLEUUID;
