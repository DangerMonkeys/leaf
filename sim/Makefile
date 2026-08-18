# Build for the Leaf device emulator.
#
# The emulator is not a reimplementation of Leaf: it compiles the real firmware sources from
# src/vario against a host stand-in for the ESP32 platform.  Only the layers that physically
# cannot exist off-device (Arduino core, ESP-IDF, the sensor drivers' I2C/SPI traffic, radios)
# are replaced; everything above them -- instruments, navigation, logging, settings, the whole
# display and menu tree -- is the shipping code.
#
# Intended to run inside the container started by run.ps1 / run.sh with the repo root mounted.
# Everything it needs is either in the repo or in .pio/libdeps (populated by a PlatformIO build).

ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
SIM     := $(ROOT)/sim
BUILD   ?= $(SIM)/build
VARIANT ?= leaf_3_2_7

# ----------------------------------------------------------------- dependencies
# Header-only libraries that PlatformIO fetches rather than vendoring.  Their directory names
# contain spaces, which make cannot express in a prerequisite, so they are linked into the build
# tree under stable names before anything compiles.
LIBDEPS := $(ROOT)/.pio/libdeps
LIBENV  := $(firstword $(wildcard $(LIBDEPS)/$(VARIANT)_release $(LIBDEPS)/$(VARIANT)_dev $(LIBDEPS)/*))
DEPLINK := $(BUILD)/deps

U8G2    := $(ROOT)/src/libraries/U8g2/src
TINYGPS := $(ROOT)/src/libraries/TinyGPSPlus-ESP32/src
MAGICEN := $(ROOT)/src/libraries/magic_enum

INCLUDES := \
  -I$(SIM)/hal/include \
  -I$(SIM)/emulator \
  -I$(ROOT)/src/vario \
  -I$(ROOT)/src/variants/$(VARIANT) \
  -I$(DEPLINK)/etl \
  -I$(DEPLINK)/igc \
  -I$(DEPLINK)/fanet \
  -I$(DEPLINK)/json \
  -I$(U8G2) \
  -I$(TINYGPS) \
  -I$(MAGICEN)

# ARDUINO_USB_* keep the firmware on the plain `Serial` path (the USBCDC swap in
# system/usb_serial.h has no meaning without USB hardware).  DEBUG_WIFI is on so the device's own
# UDP injection server is compiled in: the emulator serves it over a real socket, which is what
# lets sim/play_buslog.py drive the emulator exactly as it drives hardware.
DEFINES := \
  -DLEAF_SIM=1 \
  -DARDUINO=200 \
  -DARDUINO_USB_MODE=1 \
  -DARDUINO_USB_CDC_ON_BOOT=1 \
  -DDEBUG_WIFI \
  -DDISABLE_MASS_STORAGE \
  -DDISABLE_BATTERY_SHUTDOWN \
  -DARDUINOJSON_ENABLE_PROGMEM=0 \
  -DARDUINOJSON_ENABLE_STD_STRING=1

WARN     := -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
            -Wno-sign-compare -Wno-format-security -Wno-deprecated-declarations
OPT      ?= -O1 -g
CXXFLAGS := -std=gnu++17 -fno-strict-aliasing $(OPT) $(WARN) $(INCLUDES) $(DEFINES)
CFLAGS   := -std=gnu99 $(OPT) -w -I$(SIM)/hal/include -I$(U8G2)
FW_CFLAGS := -std=gnu99 $(OPT) -w $(INCLUDES) $(DEFINES)
LDFLAGS  := -lm -lpthread -lstdc++fs

# ----------------------------------------------------------------- firmware sources
# Everything under src/vario compiles into the emulator except the units below, which talk to
# hardware that has no host equivalent.  Each one is replaced by a same-named class implementation
# in sim/device -- the firmware headers, and therefore the interfaces the rest of the firmware
# sees, are unchanged.
FW_EXCLUDE := \
  src/vario/hardware/aht20.cpp \
  src/vario/hardware/icm_20948.cpp \
  src/vario/hardware/ms5611.cpp \
  src/vario/hardware/io_pins.cpp \
  src/vario/hardware/Leaf_I2C.cpp \
  src/vario/hardware/Leaf_SPI.cpp \
  src/vario/storage/sd_card.cpp \
  src/vario/system/usb_state.cpp \
  src/vario/system/usb_serial.cpp \
  src/vario/comms/ble.cpp \
  src/vario/comms/fanet_radio.cpp \
  src/vario/comms/ota.cpp \
  src/vario/comms/sd_firmware_update.cpp \
  src/vario/comms/webserver.cpp \
  src/vario/comms/wifi_coordinator.cpp \
  src/vario/comms/factory_discovery.cpp \
  src/vario/diagnostics/diagnostic_network/diagnostic_network.cpp

FW_ALL     := $(patsubst $(ROOT)/%,%,$(shell find $(ROOT)/src/vario -name '*.cpp'))
FW_SOURCES := $(addprefix $(ROOT)/,$(filter-out $(FW_EXCLUDE),$(FW_ALL)))
FW_C       := $(shell find $(ROOT)/src/vario -name '*.c')

# ----------------------------------------------------------------- emulator sources
HAL_SOURCES := $(shell find $(SIM)/hal/src -name '*.cpp' 2>/dev/null)
DEV_SOURCES := $(shell find $(SIM)/device -name '*.cpp' 2>/dev/null)
EMU_SOURCES := $(shell find $(SIM)/emulator -name '*.cpp' 2>/dev/null)

# Libraries compiled from source rather than stubbed.  U8g2 in particular: the pixels the emulator
# shows are the ones the device's own graphics core produces.
U8G2_C   := $(wildcard $(U8G2)/clib/*.c)
U8G2_CPP := $(U8G2)/U8g2lib.cpp $(U8G2)/U8x8lib.cpp
TINY_CPP := $(wildcard $(TINYGPS)/*.cpp)
IGC_CPP  := $(wildcard $(DEPLINK)/igc/*.cpp)

OBJ_FW   := $(patsubst $(ROOT)/%.cpp,$(BUILD)/fw/%.o,$(FW_SOURCES)) \
            $(patsubst $(ROOT)/%.c,$(BUILD)/fw/%.o,$(FW_C))
OBJ_SIM  := $(patsubst $(SIM)/%.cpp,$(BUILD)/sim/%.o,$(HAL_SOURCES) $(DEV_SOURCES) $(EMU_SOURCES))
OBJ_LIB  := $(patsubst $(U8G2)/%.c,$(BUILD)/lib/u8g2/%.o,$(U8G2_C)) \
            $(patsubst $(U8G2)/%.cpp,$(BUILD)/lib/u8g2/%.o,$(U8G2_CPP)) \
            $(patsubst $(TINYGPS)/%.cpp,$(BUILD)/lib/tinygps/%.o,$(TINY_CPP)) \
            $(patsubst $(DEPLINK)/igc/%.cpp,$(BUILD)/lib/igc/%.o,$(IGC_CPP))

OBJECTS := $(OBJ_FW) $(OBJ_SIM) $(OBJ_LIB)
TARGET  := $(BUILD)/leafsim

.PHONY: all clean deps
all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "  LD    $(notdir $@)"
	@$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo "Built $@"

$(BUILD)/fw/%.o: $(ROOT)/%.cpp | deps
	@mkdir -p $(dir $@)
	@echo "  CXX   $*"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/sim/%.o: $(SIM)/%.cpp | deps
	@mkdir -p $(dir $@)
	@echo "  CXX   sim/$*"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/lib/u8g2/%.o: $(U8G2)/%.c | deps
	@mkdir -p $(dir $@)
	@echo "  CC    u8g2/$*"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/lib/u8g2/%.o: $(U8G2)/%.cpp | deps
	@mkdir -p $(dir $@)
	@echo "  CXX   u8g2/$*"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/fw/%.o: $(ROOT)/%.c | deps
	@mkdir -p $(dir $@)
	@echo "  CC    $*"
	@$(CC) $(FW_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/lib/tinygps/%.o: $(TINYGPS)/%.cpp | deps
	@mkdir -p $(dir $@)
	@echo "  CXX   tinygps/$*"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/lib/igc/%.o: $(DEPLINK)/igc/%.cpp | deps
	@mkdir -p $(dir $@)
	@echo "  CXX   igc/$*"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

deps:
	@if [ -z "$(LIBENV)" ]; then \
	  echo "ERROR: no .pio/libdeps found."; \
	  echo "The emulator uses the same ETL / IgcLogger / ArduinoJson versions as the firmware."; \
	  echo "Run a PlatformIO build once (pio run -e $(VARIANT)_release) and try again."; \
	  exit 1; \
	fi
	@mkdir -p $(DEPLINK)
	@ln -sfn "$(LIBENV)/Embedded Template Library/include" $(DEPLINK)/etl
	@ln -sfn "$(LIBENV)/IgcLogger/src" $(DEPLINK)/igc
	@ln -sfn "$(LIBENV)/FANET Protocol implementation in c__/include" $(DEPLINK)/fanet
	@ln -sfn "$(LIBENV)/ArduinoJson/src" $(DEPLINK)/json

clean:
	@rm -rf $(BUILD)
	@echo "Cleaned $(BUILD)"

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
