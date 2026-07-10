/**
 * @file common.h
 * @brief Shared definitions for the Vehicle Fleet Management System.
 *
 * Included by C (BBG) and C++ (server) components.
 * telemetry_frame_t is 84 bytes — matches STM32 compiler natural layout.
 */

#ifndef FLEET_COMMON_H
#define FLEET_COMMON_H

#include <stdint.h>
#include <time.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── System constants ────────────────────────────────────────────────── */
#define MAX_CARS          100
#define RING_SIZE         1000
#define SERVER_PORT       9090
#define BUF_SIZE          512
#define MAX_CLIENTS       120

/* ── Wire message type bytes ─────────────────────────────────────────── */
#define MSG_TRIP_START    'S'
#define MSG_TRIP_DATA     'D'
#define MSG_TRIP_END      'E'

/* ── Default paths ───────────────────────────────────────────────────── */
#define DEFAULT_DB_PATH       "fleet.db"
#define DEFAULT_LOG_DIR       "/tmp/fleet_logs"
#define DEFAULT_ALERT_LOG     "/tmp/fleet_alerts.log"

/* ── Driving quality thresholds ─────────────────────────────────────── */
#define HARSH_BRAKE_THRESHOLD   -4.0f
#define HARSH_ACCEL_THRESHOLD    3.0f
#define HARSH_TURN_THRESHOLD    45.0f
#define SPEED_LIMIT_KMH         60.0f

/* ── Score penalty weights ───────────────────────────────────────────── */
#define PENALTY_HARSH_BRAKE    5
#define PENALTY_HARSH_ACCEL    3
#define PENALTY_HARSH_TURN     4
#define PENALTY_OVERSPEED      2

/* ─────────────────────────────────────────────────────────────────────
 * Telemetry frame — 84 bytes, natural compiler layout.
 *
 * Layout (verified with offsetof on STM32 arm-none-eabi-gcc):
 *   [0-7]   car_id          8 bytes
 *   [8-15]  driver_id       8 bytes
 *   [16-35] trip_id        20 bytes   ← 20 not 16 (matches STM32 fleet_data.h)
 *   [36-39] latitude        4 bytes
 *   [40-43] longitude       4 bytes
 *   [44-47] gps_speed_kmh   4 bytes
 *   [48-51] accel_x         4 bytes
 *   [52-55] accel_y         4 bytes
 *   [56-59] accel_z         4 bytes
 *   [60-63] gyro_x          4 bytes
 *   [64-67] gyro_y          4 bytes
 *   [68-71] gyro_z          4 bytes
 *   [72-75] timestamp_sec   4 bytes
 *   [76]    msg_type        1 byte
 *   [77]    gps_fix         1 byte
 *   [78]    satellites      1 byte
 *   [79-83] reserved        5 bytes
 *   Total = 84 bytes
 * ───────────────────────────────────────────────────────────────────── */
typedef struct {
    char     car_id[8];
    char     driver_id[8];
    char     trip_id[20];
    float    latitude;
    float    longitude;
    float    gps_speed_kmh;
    float    accel_x;
    float    accel_y;
    float    accel_z;
    float    gyro_x;
    float    gyro_y;
    float    gyro_z;
    uint32_t timestamp_sec;
    uint8_t  msg_type;
    uint8_t  gps_fix;
    uint8_t  satellites;
    uint8_t  reserved[5];
} telemetry_frame_t;

/**
 * @brief Driving quality metrics computed on BBG from one telemetry frame.
 */
typedef struct {
    int   harsh_brake;
    int   harsh_accel;
    int   harsh_turn;
    int   overspeed;
} metrics_t;

#ifdef __cplusplus
}
#endif

#endif /* FLEET_COMMON_H */
