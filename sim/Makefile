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
LIBENV  := $(firstword $(wildcard $(LIBDEPS)/$(VARIANT)_release $(LIBDEPS)/*_release))
DEPLINK := $(BUILD)/deps

U8G2    := $(ROOT)/src/libraries/U8g2/src
TINYGPS := $(ROOT)/src/libraries/TinyGPSPlus-ESP32/src
MAGICEN := $(ROOT)/src/libraries/magic_enum

INCLUDES := \
  -I$(SIM)/hal/include \
  -I$(SIM)/emulator \
  -I$(ROOT)/src/vario \
  -I$(ROOT)/src/variants/$(VARIANT) \
  -I$(ROOT)/src/libraries/LSM6DSV/src \
  -I$(DEPLINK)/etl \
  -I$(DEPLINK)/igc \
  -I$(DEPLINK)/md5 \
  -I$(DEPLINK)/fanet \
  -I$(DEPLINK)/json \
  -I$(U8G2) \
  -I$(TINYGPS) \
  -I$(MAGICEN)

# ARDUINO_USB_* keep the firmware on the plain `Serial` path (the USBCDC swap in
# system/usb_serial.h has no meaning without USB hardware). UDP injection uses the same
# runtime developer-mode and injection opt-in gates as hardware, with a host socket for WiFi.
DEFINES := \
  -DLEAF_SIM=1 \
  -DARDUINO=200 \
  -DARDUINO_USB_MODE=1 \
  -DARDUINO_USB_CDC_ON_BOOT=1 \
  -D__GLIBC_USE_C2X_STRTOL=0 \
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
  src/vario/hardware/lsm6dsv.cpp \
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
IGC_CPP  := $(wildcard $(LIBENV)/IgcLogger/src/*.cpp)

OBJ_FW   := $(patsubst $(ROOT)/%.cpp,$(BUILD)/fw/%.o,$(FW_SOURCES)) \
            $(patsubst $(ROOT)/%.c,$(BUILD)/fw/%.o,$(FW_C))
OBJ_SIM  := $(patsubst $(SIM)/%.cpp,$(BUILD)/sim/%.o,$(HAL_SOURCES) $(DEV_SOURCES) $(EMU_SOURCES))
OBJ_LIB  := $(patsubst $(U8G2)/%.c,$(BUILD)/lib/u8g2/%.o,$(U8G2_C)) \
            $(patsubst $(U8G2)/%.cpp,$(BUILD)/lib/u8g2/%.o,$(U8G2_CPP)) \
            $(patsubst $(TINYGPS)/%.cpp,$(BUILD)/lib/tinygps/%.o,$(TINY_CPP)) \
            $(patsubst $(LIBENV)/IgcLogger/src/%.cpp,$(BUILD)/lib/igc/%.o,$(IGC_CPP))

OBJECTS := $(OBJ_FW) $(OBJ_SIM) $(OBJ_LIB)
TARGET  := $(BUILD)/leafsim

.PHONY: all clean deps test-thermal-core test-free-flight test-thermal-track test-display-settings test-leaf-log-sync test-udp-injection
all: $(TARGET)

.PHONY: test-g-load
$(BUILD)/g_load_tracker_test: $(SIM)/tests/g_load_tracker.cpp $(ROOT)/src/vario/instruments/g_load_tracker.h
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++17 -Wall -Wextra -g -I$(ROOT)/src/vario $< -o $@

$(BUILD)/flight_diagnostics_test: $(SIM)/tests/flight_diagnostics.cpp $(ROOT)/src/vario/diagnostics/flight_diagnostics.h
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++17 -Wall -Wextra -g -pthread -I$(ROOT)/src/vario $< -o $@

$(BUILD)/g_load_integration_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/g_load_integration.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/g_load_integration.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

$(BUILD)/g_load_launch_test: $(SIM)/tests/g_load_launch.cpp $(ROOT)/src/vario/instruments/g_load_tracker.h $(ROOT)/src/vario/logging/flight_auto_start.h
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -o $@ $<

test-g-load: $(BUILD)/g_load_tracker_test $(BUILD)/flight_diagnostics_test $(BUILD)/g_load_launch_test $(BUILD)/g_load_integration_test
	@$(BUILD)/g_load_tracker_test
	@$(BUILD)/flight_diagnostics_test
	@$(BUILD)/g_load_launch_test
	@cd $(ROOT) && $(BUILD)/g_load_integration_test

.PHONY: test-runtime-capture
$(BUILD)/runtime_capture_test: $(SIM)/tests/runtime_capture.cpp $(ROOT)/src/vario/diagnostics/runtime_capture.h $(ROOT)/src/vario/diagnostics/gps_profile.h
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++17 -Wall -Wextra -g -I$(ROOT)/src/vario $< -o $@

test-runtime-capture: $(BUILD)/runtime_capture_test
	@$(BUILD)/runtime_capture_test

.PHONY: test-runtime-lifecycle
$(BUILD)/runtime_capture_lifecycle_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/runtime_capture_lifecycle.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/runtime_capture_lifecycle.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS) -Wl,--wrap=heap_caps_get_free_size,--wrap=heap_caps_malloc,--wrap=heap_caps_free

test-runtime-lifecycle: $(BUILD)/runtime_capture_lifecycle_test
	@cd $(ROOT) && $(BUILD)/runtime_capture_lifecycle_test

.PHONY: test-developer-logging
$(BUILD)/developer_logging_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/developer_logging.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/developer_logging.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-developer-logging: $(BUILD)/developer_logging_test
	@cd $(ROOT) && $(BUILD)/developer_logging_test

.PHONY: test-runtime-integration
$(BUILD)/runtime_capture_integration_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/runtime_capture_integration.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/runtime_capture_integration.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-runtime-integration: $(BUILD)/runtime_capture_integration_test
	@cd $(ROOT) && $(BUILD)/runtime_capture_integration_test

.PHONY: test-gps-uart-yield
$(BUILD)/gps_uart_yield_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/gps_uart_yield.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/gps_uart_yield.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-gps-uart-yield: $(BUILD)/gps_uart_yield_test
	@cd $(ROOT) && $(BUILD)/gps_uart_yield_test

$(BUILD)/udp_injection_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/udp_injection.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/udp_injection.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-udp-injection: $(BUILD)/udp_injection_test
	@cd $(ROOT) && $(BUILD)/udp_injection_test

# Regression checks use the same firmware/HAL objects, with a test entry point instead of the UI.
.PHONY: test-display-circles
$(BUILD)/display_circles_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/display_circles.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/display_circles.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-display-circles: $(BUILD)/display_circles_test
	@cd $(ROOT) && $(BUILD)/display_circles_test

.PHONY: test-thermal-track-bitmaps
$(BUILD)/thermal_track_bitmaps_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/thermal_track_bitmaps.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/thermal_track_bitmaps.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-thermal-track-bitmaps: $(BUILD)/thermal_track_bitmaps_test
	@cd $(ROOT) && $(BUILD)/thermal_track_bitmaps_test

$(BUILD)/thermal_core_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/thermal_core.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/thermal_core.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-thermal-core: $(BUILD)/thermal_core_test
	@cd $(ROOT) && $(BUILD)/thermal_core_test

$(BUILD)/free_flight_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/free_flight.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/free_flight.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-free-flight: $(BUILD)/free_flight_test
	@cd $(ROOT) && $(BUILD)/free_flight_test

$(BUILD)/thermal_track_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/thermal_track.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/thermal_track.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-thermal-track: $(BUILD)/thermal_track_test
	@cd $(ROOT) && $(BUILD)/thermal_track_test

.PHONY: test-thermal-lifecycle
$(BUILD)/thermal_lifecycle_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/thermal_lifecycle.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/thermal_lifecycle.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-thermal-lifecycle: $(BUILD)/thermal_lifecycle_test
	@cd $(ROOT) && $(BUILD)/thermal_lifecycle_test

$(BUILD)/display_settings_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/display_settings.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/display_settings.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-display-settings: $(BUILD)/display_settings_test
	@cd $(ROOT) && $(BUILD)/display_settings_test
	@cd $(ROOT) && $(BUILD)/display_settings_test --verify-persistence

# Exercise the real sync/storage code with deterministic upload outcomes, without network access.
LEAF_LOG_TEST_OBJECTS := $(filter-out $(BUILD)/sim/emulator/main.o $(BUILD)/fw/src/vario/comms/leaf_log_client.o,$(OBJECTS))

$(BUILD)/leaf_log_sync_test: $(LEAF_LOG_TEST_OBJECTS) $(SIM)/tests/leaf_log_sync.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/leaf_log_sync.cpp $(LEAF_LOG_TEST_OBJECTS) $(LDFLAGS) -Wl,--wrap=readdir,--wrap=fread,--wrap=fclose

test-leaf-log-sync: $(BUILD)/leaf_log_sync_test
	@cd $(ROOT) && $(BUILD)/leaf_log_sync_test

.PHONY: test-logbook-io
$(BUILD)/logbook_io_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/logbook_io.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/logbook_io.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-logbook-io: $(BUILD)/logbook_io_test
	@cd $(ROOT) && $(BUILD)/logbook_io_test

# Deterministic visual inventory; does not change firmware rendering or require hardware.
$(BUILD)/sd_dialogs_capture: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/sd_dialogs.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/sd_dialogs.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

.PHONY: test-usb-serial
$(BUILD)/usb_serial_test: $(SIM)/tests/usb_serial.cpp $(wildcard $(SIM)/tests/usb_serial_hal/*.h) $(ROOT)/src/vario/system/usb_serial.cpp $(ROOT)/src/vario/system/usb_serial.h $(ROOT)/src/vario/system/usb_state.cpp $(ROOT)/src/vario/system/usb_state.h
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++17 -Wall -Wextra -Werror -DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=0 -DCONFIG_IDF_TARGET_ESP32S3=0 -I$(SIM)/tests/usb_serial_hal -I$(ROOT)/src/vario -o $@ $(SIM)/tests/usb_serial.cpp $(ROOT)/src/vario/system/usb_serial.cpp $(ROOT)/src/vario/system/usb_state.cpp

test-usb-serial: $(BUILD)/usb_serial_test
	@$(BUILD)/usb_serial_test

.PHONY: test-charging-dialogs
$(BUILD)/charging_dialogs_test: $(LEAF_LOG_TEST_OBJECTS) $(SIM)/tests/charging_dialogs.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/charging_dialogs.cpp $(LEAF_LOG_TEST_OBJECTS) $(LDFLAGS)

test-charging-dialogs: $(BUILD)/charging_dialogs_test
	@cd $(ROOT) && $(BUILD)/charging_dialogs_test $(ROOT)/docs/ui-reviews/charging-dialog-revamp/screenshots

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

$(BUILD)/lib/igc/%.o: $(LIBENV)/IgcLogger/src/%.cpp | deps
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
	@ln -sfn "$(LIBENV)/MD5/src" $(DEPLINK)/md5
	@ln -sfn "$(LIBENV)/FANET Protocol implementation in c__/include" $(DEPLINK)/fanet
	@ln -sfn "$(LIBENV)/ArduinoJson/src" $(DEPLINK)/json

clean:
	@rm -rf $(BUILD)
	@echo "Cleaned $(BUILD)"

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

.PHONY: test-flight-auto-start
.PHONY: test-flight-start-window
$(BUILD)/flight_start_window_test: $(SIM)/tests/flight_start_window.cpp $(SIM)/tests/flight_start_window_checks.h $(ROOT)/src/vario/logging/flight_auto_start.h
	@mkdir -p $(@D)
	@$(CXX) -std=c++17 -Wall -Wextra -I$(ROOT)/src/vario -o $@ $(SIM)/tests/flight_start_window.cpp

test-flight-start-window: $(BUILD)/flight_start_window_test
	@$<

$(BUILD)/flight_auto_start_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/flight_auto_start.cpp $(SIM)/tests/flight_start_window_checks.h
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/flight_auto_start.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-flight-auto-start: $(BUILD)/flight_auto_start_test
	@cd $(ROOT) && $(BUILD)/flight_auto_start_test

.PHONY: test-logbook-maintenance
$(BUILD)/logbook_maintenance_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/logbook_maintenance.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/logbook_maintenance.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-logbook-maintenance: $(BUILD)/logbook_maintenance_test
	@cd $(ROOT) && $(BUILD)/logbook_maintenance_test $(BUILD)/logbook-maintenance-screens

# Delay tolerance through the actual instruments and Kalman implementation.
.PHONY: test-vario-continuity
$(BUILD)/kalman_gaps_test: $(SIM)/tests/kalman_gaps.cpp $(ROOT)/src/vario/math/kalman.cpp $(ROOT)/src/vario/math/kalman.h $(ROOT)/src/vario/instruments/vario_timing.h
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/kalman_gaps.cpp $(ROOT)/src/vario/math/kalman.cpp

$(BUILD)/vario_continuity_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/vario_continuity.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/vario_continuity.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-vario-continuity: $(BUILD)/kalman_gaps_test $(BUILD)/vario_continuity_test $(BUILD)/kalman_bias_test $(BUILD)/flat_acceleration_bias_test
	@$(BUILD)/kalman_gaps_test
	@$(BUILD)/kalman_bias_test
	@$(BUILD)/flat_acceleration_bias_test
	@cd $(ROOT) && $(BUILD)/vario_continuity_test

# The normal simulator replaces the sensor driver. This target runs the real LSM driver.
.PHONY: test-lsm6dsv-continuity
LSM_TEST_OBJECTS := $(filter-out $(BUILD)/sim/emulator/main.o $(BUILD)/sim/device/sensors.o,$(OBJECTS))
$(BUILD)/sensors_real_lsm.o: $(SIM)/device/sensors.cpp | deps
	@$(CXX) $(CXXFLAGS) -DLEAF_SIM_REAL_LSM6DSV -MMD -MP -c $< -o $@

$(BUILD)/lsm6dsv_real.o: $(ROOT)/src/vario/hardware/lsm6dsv.cpp | deps
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/lsm6dsv_registers.o: $(ROOT)/src/libraries/LSM6DSV/src/lsm6dsv_reg.c | deps
	@$(CC) $(FW_CFLAGS) -c $< -o $@

$(BUILD)/lsm6dsv_continuity_test: $(LSM_TEST_OBJECTS) $(BUILD)/sensors_real_lsm.o $(BUILD)/lsm6dsv_real.o $(BUILD)/lsm6dsv_registers.o $(SIM)/tests/lsm6dsv_continuity.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/lsm6dsv_continuity.cpp $(LSM_TEST_OBJECTS) $(BUILD)/sensors_real_lsm.o $(BUILD)/lsm6dsv_real.o $(BUILD)/lsm6dsv_registers.o $(LDFLAGS)

test-lsm6dsv-continuity: $(BUILD)/lsm6dsv_continuity_test
	@cd $(ROOT) && $(BUILD)/lsm6dsv_continuity_test

$(BUILD)/kalman_bias_test: $(SIM)/tests/kalman_bias.cpp $(ROOT)/src/vario/math/kalman.cpp $(ROOT)/src/vario/math/kalman.h
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/kalman_bias.cpp $(ROOT)/src/vario/math/kalman.cpp

$(BUILD)/flat_acceleration_bias_test: $(SIM)/tests/flat_acceleration_bias.cpp $(ROOT)/src/vario/math/kalman.cpp $(ROOT)/src/vario/math/kalman.h
	@mkdir -p $(BUILD)
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/flat_acceleration_bias.cpp $(ROOT)/src/vario/math/kalman.cpp

.PHONY: test-climb-display-average
$(BUILD)/climb_display_average_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/climb_display_average.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/climb_display_average.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-climb-display-average: $(BUILD)/climb_display_average_test
	@cd $(ROOT) && $(BUILD)/climb_display_average_test
	@cd $(ROOT) && $(BUILD)/climb_display_average_test --verify-persistence

.PHONY: test-shutdown
$(BUILD)/shutdown_policy_test: $(SIM)/tests/shutdown_policy.cpp $(ROOT)/src/vario/system/shutdown_policy.h $(ROOT)/src/vario/utils/producer_gate.h
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++17 -Wall -Wextra -g -I$(ROOT)/src/vario $< -lpthread -o $@

$(BUILD)/shutdown_rtc_test: $(SIM)/tests/shutdown_rtc.cpp $(ROOT)/src/vario/system/shutdown_guard.h $(ROOT)/src/vario/system/shutdown_rtc_policy.h
	@mkdir -p $(BUILD)
	@$(CXX) -std=c++17 -Wall -Wextra -g -I$(ROOT)/src/vario $< -o $@

$(BUILD)/shutdown_integration_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/shutdown_integration.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/shutdown_integration.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

test-shutdown: $(BUILD)/shutdown_policy_test $(BUILD)/shutdown_rtc_test $(BUILD)/shutdown_integration_test
	@$(BUILD)/shutdown_policy_test
	@$(BUILD)/shutdown_rtc_test
	@cd $(ROOT) && for mode in normal disabled lock save worker sound peripherals asleep clear latch ble hold nested watchdog rtc rtcble recovery; do $(BUILD)/shutdown_integration_test $$mode || exit $$?; done

# Build all sources consistently with the explicit developer-only menu flag.
.PHONY: test-shutdown-menu run-shutdown-menu
test-shutdown-menu:
	@$(MAKE) -C $(SIM) BUILD=$(BUILD)/shutdown-menu OPT="$(OPT) -DLEAF_SHUTDOWN_FAULT_INJECTION=1" run-shutdown-menu

$(BUILD)/shutdown_menu_test: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/tests/shutdown_menu.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/tests/shutdown_menu.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

run-shutdown-menu: $(BUILD)/shutdown_menu_test
	@cd $(ROOT) && $(BUILD)/shutdown_menu_test
