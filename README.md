# Vehicle Fleet Management System
## Complete Setup and Usage Guide

---

## Project Overview

A real-time vehicle fleet management system that monitors 100 virtual vehicles
simultaneously. One STM32 board simulates all 100 cars by cycling through
their sensor data and sending it to a BeagleBone Green (BBG) over I2C.
The BBG runs 100 threads — one per virtual car — that compute driving
metrics and forward telemetry to a fleet server on a Linux PC.

```
STM32                BBG (Linux)              PC (Linux)
──────               ───────────              ──────────
Simulates       I2C  I2C reader thread   TCP  Fleet server
100 cars   ────────► + 100 car threads ──────► scores trips
(sim_data.c)         (fleet_bbg.c)            stores to SQLite
                     ↕ pthread shared          (server_main.cpp)
                       memory only             ↕ std::queue
                                               DB thread
                                               (fleet.db)
```

### What the system measures per vehicle

| Metric | Sensor | Threshold |
|--------|--------|-----------|
| Harsh braking | Accelerometer | accel_x < −4.0 m/s² |
| Harsh acceleration | Accelerometer | accel_x > 3.0 m/s² |
| Sharp cornering | Gyroscope | \|gyro_z\| > 45 °/s |
| Speeding | GPS | speed > 60 km/h |

### Driving score (0–100 per trip)

```
Score = 100
      − harsh_brake_count  × 5
      − harsh_accel_count  × 3
      − harsh_turn_count   × 4
      − overspeed_count    × 2

Grade:  90–100  Excellent
        75–89   Good
        60–74   Acceptable
        40–59   Poor
         0–39   Dangerous
```

---

## Project File Structure

```
fleet_system/
├── common/
│   ├── common.h           Shared structs and constants (C and C++)
│   ├── Logger.hpp         C++ thread-safe logger
│   └── Config.hpp         C++ KEY=VALUE config parser
├── server/
│   ├── server_main.cpp    Entry point — 2 threads (TCP + DB)
│   ├── TelemetryParser.hpp  Parses wire messages from BBG
│   ├── TripManager.hpp    Tracks active trips, computes score on END
│   ├── TripQueue.hpp      std::queue between TCP thread and DB thread
│   ├── ViolationDetector.hpp  Real-time alert logging
│   ├── Database.hpp/.cpp  SQLite wrapper (trips, violations, drivers)
│   └── (no SharedMemory!) DB is a thread — no shmget needed
├── bbg/
│   ├── fleet_bbg.c        Main: 101 threads, fleet_shared_t
│   └── metrics.c/.h       compute_metrics() from raw sensor data
├── stm32/
│   ├── main.c             STM32 main loop — cycles 100 car IDs
│   ├── sim_data.c         Sensor simulation engine for 100 cars
│   └── sim_data.h         Simulation API header
├── config/
│   └── fleet.cfg          All configurable parameters
└── Makefile               Builds server and BBG (not STM32)
```

---

## Part 1 — Build the PC Server

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install build-essential libsqlite3-dev
```

### Build

```bash
cd fleet_system
make all
```

Expected output:
```
[OK] bin/fleet_server
[OK] bin/fleet_bbg
```

The `fleet_bbg` binary is cross-built for the BBG if you compile natively
on the BBG. If your PC and BBG have the same architecture (both ARM or
both x86) you can copy it directly. Otherwise see cross-compilation below.

---

## Part 2 — Network Setup

Connect a single Ethernet cable directly between the PC and the BBG.

### On the PC

```bash
# Find your Ethernet interface name
ip link show
# Common names: eth0, enp3s0, enp0s3

# Set a static IP (replace enp3s0 with your interface)
sudo ip addr add 192.168.10.1/24 dev enp3s0
sudo ip link set enp3s0 up
```

### On the BBG

```bash
sudo ip addr add 192.168.10.2/24 dev eth0
sudo ip link set eth0 up
```

### Test connectivity

```bash
# From PC
ping 192.168.10.2

# From BBG
ping 192.168.10.1
```

Both should respond. If not, check the cable and interface names.

---

## Part 3 — STM32 Setup (STM32CubeIDE)

### CubeMX peripheral configuration

Open your `.ioc` file and verify:

| Peripheral | Setting | Value |
|-----------|---------|-------|
| I2C2 | Mode | I2C (slave) |
| I2C2 | Own Address 1 | 16 (= 0x08 << 1) |
| I2C2 | Timing | 0x00303D5B |
| TIM2 | Prescaler | 15999 |
| TIM2 | Counter Period | 999 |
| TIM2 | Global interrupt | Enabled (NVIC tab) |
| USART3 | Mode | Asynchronous |
| USART3 | Baud rate | 115200 |

Timer: 16 MHz HSI / 16000 / 1000 = 1 Hz interrupt (1 second tick)

### Add the simulation files to your CubeIDE project

1. Copy `stm32/main.c`     → `Core/Src/main.c`     (replace existing)
2. Copy `stm32/sim_data.c` → `Core/Src/sim_data.c`  (new file)
3. Copy `stm32/sim_data.h` → `Core/Inc/sim_data.h`  (new file)

CubeIDE picks up new `.c` files in `Core/Src/` automatically.

### Build and flash

```
Project → Build All  (Ctrl+B)
Run → Debug          (or Run → Run to flash without debug)
```

### Wiring STM32 ↔ BBG

```
STM32 NUCLEO             BeagleBone Green
────────────             ────────────────
PB10 (SCL) ──[4.7kΩ to 3.3V]── P9_19 (SCL, I2C2)
PB11 (SDA) ──[4.7kΩ to 3.3V]── P9_20 (SDA, I2C2)
GND        ─────────────────── P9_1  (GND)
```

**Pull-up resistors are mandatory.** Without them I2C will not work.

### Verify STM32 is working

Open a serial terminal at 115200 baud:

```bash
# Linux/Mac
screen /dev/ttyACM0 115200
# or
minicom -b 115200 -D /dev/ttyACM0
```

Expected output:
```
=== Vehicle Fleet STM32 Simulator started ===
Cars        : 100 virtual vehicles
Frame size  : 80 bytes
Slave addr  : 0x08
Sweep rate  : ~1 second per full cycle

[STATS] Total frames sent: 100 | Cars simulated: 100
[STATS] Total frames sent: 200 | Cars simulated: 100
```

---

## Part 4 — BBG Setup

### Copy the BBG binary

```bash
# Option A: compile directly on the BBG
scp fleet_system/bbg/fleet_bbg.c  debian@192.168.10.2:~
scp fleet_system/bbg/metrics.c    debian@192.168.10.2:~
scp fleet_system/bbg/metrics.h    debian@192.168.10.2:~
scp fleet_system/common/common.h  debian@192.168.10.2:~

# On BBG:
gcc fleet_bbg.c metrics.c -o fleet_bbg -lpthread -lm -I.

# Option B: copy the pre-built binary (if architectures match)
scp fleet_system/bin/fleet_bbg debian@192.168.10.2:~
```

---

## Running the Full System

Start components in this order:

### Step 1 — PC: Start the fleet server

```bash
cd fleet_system
./bin/fleet_server config/fleet.cfg
```

Expected output:
```
=== Vehicle Fleet Management Server starting ===
Port      : 9090
Database  : fleet.db
Alert log : /tmp/fleet_alerts.log

[INFO ] Fleet database opened: fleet.db
[INFO ] Waiting for BBG connections...
```

Leave this terminal open.

### Step 2 — STM32: Power on the board

The STM32 starts cycling through 100 cars immediately after reset.
You should see the "Simulator started" message on the serial terminal.

### Step 3 — BBG: Start the fleet gateway

```bash
# On the BBG
sudo ./fleet_bbg 192.168.10.1 9090
```

Expected output:
```
=== Vehicle Fleet BBG Gateway starting ===
Server : 192.168.10.1:9090
Cars   : 100 virtual vehicles
Threads: 1 I2C reader + 100 car threads

[I2C] Opened /dev/i2c-2, slave=0x08
[I2C] Reader thread started
[CAR] Thread started for CAR-001
[CAR] Thread started for CAR-002
...
[CAR-001] TCP connected
[CAR-002] TCP connected
...
[I2C] 100 frames received
```

### Step 4 — Watch it running

**On the PC — watch live log:**
```bash
tail -f /tmp/fleet_logs/server.log
```

You will see:
```
[INFO ] Car connected from 192.168.10.2
[INFO ] Trip complete: T1751234601 car=CAR-001 score=87 (Good)
[INFO ] Trip complete: T1751234623 car=CAR-007 score=62 (Acceptable)
[INFO ] Trip saved: T1751234601 score=87 (Good)
```

**Watch violations in real time:**
```bash
tail -f /tmp/fleet_alerts.log
```

You will see:
```
[2026-07-01 14:32:01] HARSH_BRAKE  CAR-042  DRV-042  lat=32.09 lon=34.78 val=-5.5 m/s²
[2026-07-01 14:32:15] OVERSPEED    CAR-017  DRV-017  lat=32.11 lon=34.81 val=87.3 km/h
[2026-07-01 14:32:44] HARSH_TURN   CAR-033  DRV-033  lat=31.77 lon=35.21 val=62.4 °/s
```

---

## Querying Results

### See all completed trips

```bash
sqlite3 fleet.db "SELECT car_id, driver_id, score, grade, harsh_brake_count,
                         overspeed_count, max_speed_kmh
                  FROM trips
                  ORDER BY score DESC;"
```

### Driver leaderboard

```bash
sqlite3 -column -header fleet.db \
  "SELECT driver_id, total_trips, ROUND(avg_score,1) AS avg_score,
          ROUND(total_km,1) AS total_km
   FROM drivers
   ORDER BY avg_score DESC
   LIMIT 20;"
```

### Worst drivers (most violations)

```bash
sqlite3 fleet.db \
  "SELECT driver_id, COUNT(*) AS violations, type
   FROM violations
   GROUP BY driver_id, type
   ORDER BY violations DESC
   LIMIT 20;"
```

### See all violations for one car

```bash
sqlite3 fleet.db \
  "SELECT timestamp, type, severity, latitude, longitude
   FROM violations
   WHERE car_id = 'CAR-042'
   ORDER BY timestamp;"
```

### Fleet statistics

```bash
sqlite3 fleet.db "SELECT
    COUNT(*)                    AS total_trips,
    ROUND(AVG(score), 1)        AS fleet_avg_score,
    MIN(score)                  AS worst_score,
    MAX(score)                  AS best_score,
    SUM(harsh_brake_count)      AS total_harsh_brakes,
    SUM(overspeed_count)        AS total_overspeed
  FROM trips;"
```

---

## Configuration

Edit `config/fleet.cfg` — no recompilation needed:

```ini
# Server
PORT        = 9090
DB_PATH     = fleet.db
LOG_FILE    = /tmp/fleet_logs/server.log
ALERT_LOG   = /tmp/fleet_alerts.log

# Driving quality thresholds
HARSH_BRAKE_THRESHOLD  = -4.0    # m/s²  (more negative = stricter)
HARSH_ACCEL_THRESHOLD  =  3.0    # m/s²
HARSH_TURN_THRESHOLD   = 45.0    # °/s
SPEED_LIMIT_KMH        = 60.0    # km/h
```

Restart the server after changing the config.

---

## Simulation Details

The STM32 simulates 100 cars with these characteristics:

| Parameter | Value |
|-----------|-------|
| Cars | CAR-001 to CAR-100 |
| Drivers | DRV-001 to DRV-100 |
| Starting locations | 10 cities across Israel, 10 cars per city |
| Trip duration | 1–5 minutes (random per car) |
| Rest between trips | 10–30 seconds (random per car) |
| Normal speed | 30–65 km/h |
| Harsh brake probability | 8% per second |
| Sharp turn probability | 10% per second |
| Overspeed probability | 7% per second |
| I2C frame size | 80 bytes |
| Frames per second | 100 (one per car) |

Cars are staggered so they do not all start their trips at the same
moment — this avoids a burst of 100 simultaneous START messages.

---

## Stopping the System

```bash
# Stop the server (Ctrl-C or)
kill $(cat /tmp/fleet_server.pid 2>/dev/null)

# Stop the BBG gateway (Ctrl-C or)
sudo kill $(pgrep fleet_bbg)
```

The server shuts down cleanly — all pending trip records in the queue
are flushed to SQLite before the DB thread exits.

---

## Cross-Compilation for BBG

If building on an x86 PC for the BBG (ARM):

```bash
# Install ARM cross-compiler
sudo apt-get install gcc-arm-linux-gnueabihf

# Build BBG binary for ARM
arm-linux-gnueabihf-gcc fleet_bbg.c metrics.c \
    -o fleet_bbg_arm \
    -lpthread -lm \
    -I../common

# Copy to BBG
scp fleet_bbg_arm debian@192.168.10.2:~/fleet_bbg
```

---

## Troubleshooting

| Problem | Likely cause | Fix |
|---------|-------------|-----|
| BBG prints `[ERR] I2C read` | STM32 not running or not wired | Check power, flash, pull-up resistors |
| BBG prints `[ERR] connect failed` | Server not running or wrong IP | Start server first, check IP |
| Server shows no trips | BBG not started | Start `./fleet_bbg` on BBG |
| All scores are 100 | Thresholds too strict | Lower thresholds in fleet.cfg |
| `make: sqlite3.h not found` | Missing dev package | `sudo apt-get install libsqlite3-dev` |
| No data in fleet.db | DB thread error | Check `/tmp/fleet_logs/server.log` |
| STM32 hangs after reset | I2C config wrong | Verify OwnAddress1=16, Timing=0x00303D5B |

---
- `TripManager.hpp` — trip accumulator and scorer
- `TripQueue.hpp` — replaces POSIX shared memory with `std::queue`
- `ViolationDetector.hpp` — real-time alert logging
