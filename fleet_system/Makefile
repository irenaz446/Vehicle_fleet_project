# ══════════════════════════════════════════════════════════════
# Vehicle Fleet Management System — Makefile
#
# Targets:
#   all      Build server + bbg (x86) + bbg-arm if cross-compiler found
#   server   Build fleet server (C++, x86)
#   bbg      Build BBG gateway (C, x86 — for local testing)
#   bbg-arm  Build BBG gateway for BeagleBone Green (ARM)
#            Requires: sudo apt-get install gcc-arm-linux-gnueabihf
#   clean    Remove bin/
#
# STM32: build in STM32CubeIDE — NOT by this Makefile.
# ══════════════════════════════════════════════════════════════

CXX        := g++
CC         := gcc
CC_ARM     := arm-linux-gnueabihf-gcc

CXXFLAGS   := -std=c++17 -Wall -Wextra -g -O2 -I./common
CFLAGS     := -Wall -Wextra -g -O2 -I./common
CFLAGS_ARM := -Wall -Wextra -g -O2 -I./common

LIBS       := -lpthread -lm
SQLIBS     := -lsqlite3

BBG_SRC    := bbg/fleet_bbg.c bbg/metrics.c
BBG_HDR    := bbg/fleet_bbg.h bbg/metrics.h common/common.h

SERVER_SRC := server/server_main.cpp server/Database.cpp
SERVER_HDR := server/TelemetryParser.hpp server/TripManager.hpp \
              server/TripQueue.hpp server/ViolationDetector.hpp \
              server/Database.hpp \
              common/common.h common/Logger.hpp common/Config.hpp

# Detect ARM cross-compiler
ARM_CC_FOUND := $(shell which $(CC_ARM) 2>/dev/null)

.PHONY: all server bbg bbg-arm clean

all: server bbg bbg-arm
	@echo ""
	@echo "=== Build complete ==="
	@echo "  bin/fleet_server      — run on Linux PC"
	@echo "  bin/fleet_bbg         — x86 only (local testing)"
ifdef ARM_CC_FOUND
	@echo "  bin/fleet_bbg_arm     — copy to BeagleBone Green:"
	@echo "    scp bin/fleet_bbg_arm debian@192.168.7.2:~/fleet_bbg"
	@echo "    ssh debian@192.168.7.2"
	@echo "    sudo ./fleet_bbg 192.168.7.1 9090"
else
	@echo "  bin/fleet_bbg_arm     — SKIPPED (cross-compiler not installed)"
	@echo "    To build: sudo apt-get install gcc-arm-linux-gnueabihf"
	@echo "              make bbg-arm"
endif
	@echo "  STM32                 — build in STM32CubeIDE"

# ── Fleet Server (C++, x86) ────────────────────────────────────
server: bin/fleet_server

bin/fleet_server: $(SERVER_SRC) $(SERVER_HDR) | bin
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LIBS) $(SQLIBS)
	@echo "[OK] bin/fleet_server"

# ── BBG Gateway — x86 (local testing) ─────────────────────────
bbg: bin/fleet_bbg

bin/fleet_bbg: $(BBG_SRC) $(BBG_HDR) | bin
	$(CC) $(CFLAGS) -o $@ $(BBG_SRC) $(LIBS)
	@echo "[OK] bin/fleet_bbg  (x86)"

# ── BBG Gateway — ARM (BeagleBone Green) ──────────────────────
bbg-arm:
ifdef ARM_CC_FOUND
	$(MAKE) bin/fleet_bbg_arm
else
	@echo "[SKIP] fleet_bbg_arm — install gcc-arm-linux-gnueabihf first"
endif

bin/fleet_bbg_arm: $(BBG_SRC) $(BBG_HDR) | bin
	$(CC_ARM) $(CFLAGS_ARM) -o $@ $(BBG_SRC) $(LIBS)
	@echo "[OK] bin/fleet_bbg_arm  (ARM — for BeagleBone Green)"

# ── Helpers ────────────────────────────────────────────────────
bin:
	mkdir -p bin

clean:
	rm -rf bin
	@echo "[OK] cleaned"
