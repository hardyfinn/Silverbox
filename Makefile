# Silverbox for VCV Rack.
# Build:  export RACK_DIR=/path/to/Rack-SDK && make && make install   (or make dist)
# Tests:  make test   (host build of the DSP and sequencer, no Rack SDK needed)

RACK_DIR ?= ../..

CXXFLAGS += -Wall -Wextra $(EXTRA_CXXFLAGS)

SOURCES += $(wildcard src/*.cpp src/modules/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += README.md

ifneq ($(wildcard $(RACK_DIR)/plugin.mk),)
include $(RACK_DIR)/plugin.mk
endif

TEST_CXX ?= g++
build/silverbox_tests: $(wildcard tests/*.cpp tests/*.hpp src/dsp/*.hpp src/seq/*.hpp)
	@mkdir -p build
	$(TEST_CXX) -std=c++17 -O2 -Wall -Wextra -o $@ $(wildcard tests/*.cpp)

.PHONY: test panels render
test: build/silverbox_tests
	./build/silverbox_tests

# Regenerate res/*.svg and src/generated/Layout.hpp (needs Python 3 and fonttools).
panels:
	python3 scripts/make_panels.py

# Render the demo pattern through the voice to tests/out/demo.wav
render: build/silverbox_tests
	./build/silverbox_tests --render
