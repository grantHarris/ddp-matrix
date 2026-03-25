# Thin wrapper around the CMake build so the old make workflow still works.

BUILD_DIR ?= build
CMAKE ?= cmake
CMAKE_ARGS ?=

all: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_ARGS)

install: configure
	$(CMAKE) --build $(BUILD_DIR)
	$(CMAKE) --install $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all configure install clean
