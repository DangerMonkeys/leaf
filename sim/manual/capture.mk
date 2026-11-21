# Run from the repository root after the normal simulator build:
# make -f sim/Makefile -f sim/manual/capture.mk manual-captures
$(BUILD)/manual_capture: $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(SIM)/manual/capture.cpp
	@$(CXX) $(CXXFLAGS) -o $@ $(SIM)/manual/capture.cpp $(filter-out $(BUILD)/sim/emulator/main.o,$(OBJECTS)) $(LDFLAGS)

.PHONY: manual-captures
manual-captures: $(BUILD)/manual_capture $(BUILD)/charging_dialogs_test
	@mkdir -p $(BUILD)/manual-raw
	@cd $(ROOT) && $(BUILD)/manual_capture
	@cd $(ROOT) && $(BUILD)/charging_dialogs_test $(BUILD)/manual-charging
