# ══════════════════════════════════════════════════════════════
# Fleet Management System — Makefile
#
# Targets:
#   all      Build server + bbg
#   server   Build fleet server (C++)
#   bbg      Build BBG gateway (C)
#   clean    Remove bin/
#
# STM32 files (stm32/main.c + stm32/sim_data.c + stm32/sim_data.h):
#   Add main.c and sim_data.c to Core/Src/ in STM32CubeIDE project.
#   Add sim_data.h to Core/Inc/ (or same folder as main.c).
#   Build with STM32CubeIDE — NOT by this Makefile.
# ══════════════════════════════════════════════════════════════

CXX      := g++
CC       := gcc

CXXFLAGS := -std=c++17 -Wall -Wextra -g -O2 -I./common
CFLAGS   := -Wall -Wextra -g -O2 -I./common

LIBS     := -lpthread -lm
SQLIBS   := -lsqlite3

.PHONY: all server bbg clean

all: server bbg
	@echo ""
	@echo "Build complete."
	@echo "  Server : ./bin/fleet_server config/fleet.cfg"
	@echo "  BBG    : sudo ./bin/fleet_bbg <SERVER_IP> 9090"
	@echo "  NOTE   : STM32 code built in STM32CubeIDE"

# ── Fleet Server (C++) ─────────────────────────────────────────
SERVER_SRC := server/server_main.cpp server/Database.cpp
SERVER_HDR := server/TelemetryParser.hpp server/TripManager.hpp \
              server/TripQueue.hpp server/ViolationDetector.hpp \
              server/Database.hpp \
              common/common.h common/Logger.hpp common/Config.hpp

server: bin/fleet_server

bin/fleet_server: $(SERVER_SRC) $(SERVER_HDR) | bin
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LIBS) $(SQLIBS)
	@echo "[OK] bin/fleet_server"

# ── BBG Gateway (C) ────────────────────────────────────────────
BBG_SRC := bbg/fleet_bbg.c bbg/metrics.c

bbg: bin/fleet_bbg

bin/fleet_bbg: $(BBG_SRC) bbg/metrics.h common/common.h | bin
	$(CC) $(CFLAGS) -o $@ $(BBG_SRC) $(LIBS)
	@echo "[OK] bin/fleet_bbg"

# ── Helpers ────────────────────────────────────────────────────
bin:
	mkdir -p bin

clean:
	rm -rf bin
	@echo "[OK] cleaned"
