# Vehicle Fleet Management System
## Complete Setup and Usage Guide

---

## Project Overview

A real-time vehicle fleet management system that monitors 100 virtual
vehicles simultaneously. One STM32 board simulates all 100 cars by
cycling through their sensor data and sending it to a BeagleBone Green
(BBG) over I2C. The BBG runs 100 threads — one per virtual car — that
compute driving metrics and forward telemetry to a fleet server running
on a Linux PC (VirtualBox VM).

```
STM32                    BBG (Linux)                PC / VirtualBox (Linux)
──────                   ───────────                ───────────────────────
Simulates           I2C  I2C reader thread     TCP  Fleet server (C++)
100 cars      ─────────► + 100 car threads  ───────► scores trips
(fleet_data.c)             (fleet_bbg.c)               stores to SQLite
                         ↕ pthread shared             (server_main.cpp)
                           memory (in-process)        ↕ std::queue
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
      − overspeed_count    × 2   (minimum 0)

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
│   │                      telemetry_frame_t = 84 bytes
│   ├── Logger.hpp         C++ thread-safe logger
│   └── Config.hpp         C++ KEY=VALUE config parser
├── server/
│   ├── server_main.cpp    Entry point — TCP thread + DB thread
│   ├── TelemetryParser.hpp  Parses wire messages from BBG
│   ├── TripManager.hpp    Tracks active trips, computes score on END
│   ├── TripQueue.hpp      std::queue between TCP and DB threads
│   ├── ViolationDetector.hpp  Real-time alert logging
│   └── Database.hpp/.cpp  SQLite wrapper (trips, violations, drivers)
├── bbg/
│   ├── fleet_bbg.h        Shared structs for BBG gateway
│   ├── fleet_bbg.c        Main: 101 threads, fleet_shared_t
│   └── metrics.c/.h       compute_metrics() from raw sensor data
├── stm32/
│   ├── main.c             STM32 main loop — cycles 100 car IDs
│   ├── fleet_data.c         Sensor simulation engine for 100 cars
│   └── fleet_data.h         Simulation API header
├── config/
│   └── fleet.cfg          Server configuration
└── Makefile               Builds server, bbg (x86), bbg-arm (ARM)
```

---

## Telemetry Frame Format (84 bytes)

The STM32 sends one 84-byte frame per car per second over I2C.
The layout is identical on STM32, BBG, and server — no packing pragma
needed because the natural compiler alignment produces the same layout
on all three targets (arm-none-eabi-gcc, arm-linux-gnueabihf-gcc, gcc).

```
Offset  Size  Field           Description
──────  ────  ─────────────── ────────────────────────────────
 [0]     8    car_id          "CAR-001" to "CAR-100"
 [8]     8    driver_id       "DRV-001" to "DRV-100"
 [16]   20    trip_id         "T20260701001" (20 bytes)
 [36]    4    latitude        GPS latitude (float, degrees)
 [40]    4    longitude       GPS longitude (float, degrees)
 [44]    4    gps_speed_kmh   Speed from GPS (float, km/h)
 [48]    4    accel_x         Forward acceleration (float, m/s²)
 [52]    4    accel_y         Lateral acceleration (float, m/s²)
 [56]    4    accel_z         Vertical acceleration (float, m/s²)
 [60]    4    gyro_x          Pitch rate (float, °/s)
 [64]    4    gyro_y          Roll rate (float, °/s)
 [68]    4    gyro_z          Yaw/turn rate (float, °/s)
 [72]    4    timestamp_sec   Unix timestamp (uint32_t)
 [76]    1    msg_type        'S'=start  'D'=data  'E'=end
 [77]    1    gps_fix         0=none  1=fix
 [78]    1    satellites      Number of satellites in view
 [79]    5    reserved        Future use
──────  ────
Total  84 bytes
```

> **Note:** `trip_id` is 20 bytes because that is what the STM32
> CubeIDE compiler produces for this field. The BBG and server
> use the same 20-byte size to guarantee binary compatibility.

---

## Part 1 — Network Setup

You need the PC (running in VirtualBox) and the BBG to communicate
over Ethernet. Connect a network cable between them.

### On the BBG

SSH into the BBG (default IP over USB: `192.168.7.2`):

```bash
ssh debian@192.168.7.2
# password: temppwd
```

Set a static IP on the BBG Ethernet port:

```bash
sudo ip addr add 192.168.10.2/24 dev eth0
sudo ip link set eth0 up
```

To make this permanent across reboots, edit `/etc/network/interfaces`:

```bash
sudo nano /etc/network/interfaces
```

Add these lines:
```
auto eth0
iface eth0 inet static
    address 192.168.10.2
    netmask 255.255.255.0
```

### On VirtualBox (Linux VM — your server machine)

First find your Ethernet interface name:

```bash
ip link show
# Look for something like: enp0s3, eth0, enx...
```

Set a static IP on the interface connected to the BBG:

```bash
# Replace enp0s3 with your actual interface name
sudo ip addr add 192.168.10.1/24 dev enp0s3
sudo ip link set enp0s3 up
```

### Test the connection

```bash
# From VirtualBox — ping BBG
ping 192.168.10.2

# From BBG — ping VirtualBox
ping 192.168.10.1
```

Both should reply. If not, check the cable and interface names.

---

## Part 2 — Build on the PC (VirtualBox)

### Install prerequisites

```bash
sudo apt-get update
sudo apt-get install build-essential libsqlite3-dev

# For ARM cross-compilation (to build the BBG binary):
sudo apt-get install gcc-arm-linux-gnueabihf
```

### Build all binaries

```bash
cd fleet_system
make all
```

Expected output:
```
[OK] bin/fleet_server      (x86 — runs on PC)
[OK] bin/fleet_bbg         (x86 — for local testing only)
[OK] bin/fleet_bbg_arm     (ARM — copy this to BBG)

=== Build complete ===
  bin/fleet_server      — run on Linux PC
  bin/fleet_bbg_arm     — copy to BeagleBone Green:
    scp bin/fleet_bbg_arm debian@192.168.7.2:~/fleet_bbg
```

> If `gcc-arm-linux-gnueabihf` is not installed, `fleet_bbg_arm` is
> skipped with a message — run `make bbg-arm` after installing it.

Individual targets:
```bash
make server      # build only the fleet server
make bbg         # build BBG binary for x86 (local test)
make bbg-arm     # build BBG binary for ARM (BeagleBone)
make clean       # remove bin/
```

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

Timer gives exactly 1 Hz: 16 MHz HSI / 16000 / 1000 = 1 Hz

### Add fleet files to your CubeIDE project

1. Copy `stm32/main.c`      → `Core/Src/main.c`      (replace existing)
2. Copy `stm32/fleet_data.c` → `Core/Src/fleet_data.c`  (new file)
3. Copy `stm32/fleet_data.h` → `Core/Inc/fleet_data.h`  (new file)

CubeIDE picks up new `.c` files in `Core/Src/` automatically.

### Frame size verification

The STM32 code prints the frame size on startup. Open a serial
terminal at 115200 baud and verify:

```
=== Vehicle Fleet STM32 Simulator started ===
[I2C] sizeof(telemetry_frame_t)=84 OK
```

If you see a different size, the struct layout in your `fleet_data.h`
does not match the project. Make sure `trip_id` is declared as
`char trip_id[20]` in both files.

### Build and flash

```
Project → Build All  (Ctrl+B)
Run → Debug          (flash and start)
```

### Wiring STM32 to BBG

```
STM32 NUCLEO             BeagleBone Green
────────────             ────────────────
PB10 (SCL) ──[4.7kΩ to 3.3V]── P9_19 (SCL, I2C2)
PB11 (SDA) ──[4.7kΩ to 3.3V]── P9_20 (SDA, I2C2)
GND        ─────────────────── P9_1  (GND)
```

Pull-up resistors (4.7kΩ from SCL to 3.3V and SDA to 3.3V)
are mandatory — I2C will not work without them.

---

## Part 4 — Deploy BBG Binary

Copy the ARM binary from your PC to the BBG:

```bash
# Run on your PC
scp bin/fleet_bbg_arm debian@192.168.7.2:~/fleet_bbg
```

Verify it runs on the BBG:

```bash
ssh debian@192.168.7.2
file ~/fleet_bbg
# Should show: ELF 32-bit LSB executable, ARM
```

---

## Part 5 — Running the Full System

Start components in this exact order:

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

Fleet database opened: fleet.db
Fleet database schema verified
Waiting for BBG connections...
```

Leave this terminal open.

### Step 2 — STM32: Power on the board

Reset or power on the STM32. Verify on serial terminal:
```
=== Vehicle Fleet STM32 Simulator started ===
Cars        : 100 virtual vehicles
Frame size  : 84 bytes
[I2C] sizeof(telemetry_frame_t)=84 OK
Waiting for BBG master...
```

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

[I2C] Opened /dev/i2c-2, slave=0x08, frame=84 bytes
[I2C] sizeof(telemetry_frame_t)=84 OK
[I2C] Reader thread started
[CAR-001] TCP connected
[CAR-002] TCP connected
...
[I2C] 100 frames received
```

### Step 4 — Watch live activity

```bash
# On PC — live server log
tail -f /tmp/fleet_logs/server.log

# Watch violations in real time
tail -f /tmp/fleet_alerts.log
```

You will see:
```
[INFO] Trip complete: T1751234601 car=CAR-001 score=87 (Good)
[INFO] Trip saved: T1751234601 score=87 (Good)
```

And in the alert log:
```
[2026-07-01 14:32:01] HARSH_BRAKE  CAR-042  DRV-042  val=-5.5 m/s²
[2026-07-01 14:32:15] OVERSPEED    CAR-017  DRV-017  val=87.3 km/h
```

---

## Part 6 — Viewing the Database

### Command line

```bash
# Open the database
sqlite3 fleet.db

# Enable readable output
.headers on
.mode column

# Show all tables (run this first to confirm schema exists)
.tables

# All completed trips
SELECT car_id, driver_id, score, grade,
       harsh_brake_count, overspeed_count
FROM trips
ORDER BY score DESC;

# Driver leaderboard
SELECT driver_id, total_trips,
       ROUND(avg_score,1) AS avg_score,
       ROUND(total_km,1) AS total_km
FROM drivers
ORDER BY avg_score DESC
LIMIT 10;

# Recent violations
SELECT timestamp, car_id, type, severity
FROM violations
ORDER BY timestamp DESC
LIMIT 20;

# Fleet summary
SELECT COUNT(*)              AS total_trips,
       ROUND(AVG(score),1)  AS fleet_avg_score,
       MIN(score)           AS worst,
       MAX(score)           AS best
FROM trips;

# Exit
.quit
```

### GUI (recommended)

```bash
sudo apt-get install sqlitebrowser
sqlitebrowser fleet.db
```

Gives a visual spreadsheet view of all tables with no SQL needed.

---

## Configuration

Edit `config/fleet.cfg` — no recompilation needed:

```ini
PORT        = 9090
DB_PATH     = fleet.db
LOG_FILE    = /tmp/fleet_logs/server.log
ALERT_LOG   = /tmp/fleet_alerts.log

# Driving quality thresholds
HARSH_BRAKE_THRESHOLD  = -4.0    # m/s²
HARSH_ACCEL_THRESHOLD  =  3.0    # m/s²
HARSH_TURN_THRESHOLD   = 45.0    # °/s
SPEED_LIMIT_KMH        = 60.0    # km/h
```

---

## Stopping the System

```bash
# Ctrl-C in the server terminal, or:
kill $(pgrep fleet_server)

# On BBG:
sudo kill $(pgrep fleet_bbg)
```

The server shuts down cleanly — all pending trips in the queue
are written to SQLite before the DB thread exits.

---

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| `no such table: trips` | Server not started yet | Run `./bin/fleet_server` first |
| `fleet.db` is 0 bytes | Server crashed before schema | Check server log |
| BBG: `Syntax error: word unexpected` | Wrong architecture binary | Use `bin/fleet_bbg_arm`, not `bin/fleet_bbg` |
| BBG: `[WARN] Bad msg_type: 0x11` | Frame size mismatch | Verify `sizeof(telemetry_frame_t)=84` on both sides |
| BBG: `[ERR] I2C read` | STM32 not running | Power on STM32 first |
| BBG: `connect failed` | Server not running or wrong IP | Check `192.168.10.1:9090` is reachable |
| `ping 192.168.10.1` fails | IP not set on PC | `sudo ip addr add 192.168.10.1/24 dev <iface>` |
| `ping 192.168.10.2` fails | IP not set on BBG | `sudo ip addr add 192.168.10.2/24 dev eth0` |
| `make bbg-arm` skipped | Cross-compiler missing | `sudo apt-get install gcc-arm-linux-gnueabihf` |
| Server: no trips in DB | No complete trip yet | Wait for STM32 to complete one full S→D→E cycle |
