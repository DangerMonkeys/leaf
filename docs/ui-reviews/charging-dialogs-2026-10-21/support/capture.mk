# Run from /leaf: make -f docs/ui-reviews/charging-dialogs-2026-10-21/support/capture.mk capture-catalog
include /leaf/sim/Makefile
CATALOG := /leaf/docs/ui-reviews/charging-dialogs-2026-10-21
CATALOG_OBJECTS := $(filter-out $(BUILD)/sim/emulator/main.o $(BUILD)/fw/src/vario/comms/leaf_log_client.o $(BUILD)/sim/device/sd_card.o $(BUILD)/sim/device/usb.o,$(OBJECTS))

$(BUILD)/charging_dialog_catalog: $(CATALOG_OBJECTS) $(CATALOG)/support/capture.cpp $(CATALOG)/support/sd_card.cpp $(CATALOG)/support/usb.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(CATALOG)/support/capture.cpp $(CATALOG)/support/sd_card.cpp $(CATALOG)/support/usb.cpp $(CATALOG_OBJECTS) $(LDFLAGS)

.PHONY: capture-catalog
capture-catalog: $(BUILD)/charging_dialog_catalog
	cd /leaf && $(BUILD)/charging_dialog_catalog $(CATALOG)
