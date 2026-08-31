.DEFAULT_GOAL := all

HOST_CXX ?= g++
HOST_TEST_DIR := build-host
HOST_TEST_BIN := $(HOST_TEST_DIR)/foundation_smoke

.PHONY: all clean clean-host test-host

# Keep the devkitPro build in the existing Makefile. GNU make prefers this
# GNUmakefile, which lets host-only tests run even when DEVKITARM is not set.
all:
	@$(MAKE) --no-print-directory -f Makefile all

clean:
	@rm -rf $(HOST_TEST_DIR)
	@if [ -n "$$DEVKITARM" ]; then \
		$(MAKE) --no-print-directory -f Makefile clean; \
	else \
		echo "DEVKITARM not set; skipped 3DS clean (host test output removed)"; \
	fi

clean-host:
	@rm -rf $(HOST_TEST_DIR)

test-host:
	@mkdir -p $(HOST_TEST_DIR)
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror \
		-Isrc \
		tests/foundation_smoke.cpp \
		src/presentation_state.cpp \
		src/stream_profile_catalog.cpp \
		src/stream_telemetry.cpp \
		-o $(HOST_TEST_BIN)
	$(HOST_TEST_BIN)

# Forward explicit 3DS targets to the original devkitPro Makefile.
%:
	@$(MAKE) --no-print-directory -f Makefile $@
