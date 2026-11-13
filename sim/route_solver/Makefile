CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Werror -fstack-usage
BUILD ?= ../build/route-solver
.PHONY: all test test-guidance sanitize
all: test test-guidance
$(BUILD):
	mkdir -p $@
$(BUILD)/tests: tests.cpp solver.h | $(BUILD)
	$(CXX) $(CXXFLAGS) tests.cpp -o $@
test: $(BUILD)/tests
	$(BUILD)/tests
sanitize: | $(BUILD)
	$(CXX) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer tests.cpp -o $(BUILD)/tests-sanitize
	$(BUILD)/tests-sanitize

$(BUILD)/guidance-tests: guidance_tests.cpp guidance.h solver.h | $(BUILD)
	$(CXX) $(CXXFLAGS) guidance_tests.cpp -o $@
test-guidance: $(BUILD)/guidance-tests
	$(BUILD)/guidance-tests
