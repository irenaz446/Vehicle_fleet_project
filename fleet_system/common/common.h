/**
 * @file common.h
 * @brief Shared definitions for the fleet management system.
 *
 * Included by C (BBG, fleet_report) and C++ (server) components.
 * Uses extern "C" guard so C++ files can include it safely.
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
#define RING_SIZE         1000    /* history ring buffer capacity         */
#define SERVER_PORT       9090
#define BUF_SIZE          512
#define MAX_CLIENTS       120     /* 100 cars + margin                    */

/* ── Wire message type bytes ─────────────────────────────────────────── */
#define MSG_TRIP_START    'S'     /* first frame of a trip                */
#define MSG_TRIP_DATA     'D'     /* telemetry sample during trip         */
#define MSG_TRIP_END      'E'     /* last frame, trip complete            */

/* ── Default paths ───────────────────────────────────────────────────── */
#define DEFAULT_DB_PATH       "fleet.db"
#define DEFAULT_LOG_DIR       "/tmp/fleet_logs"
#define DEFAULT_ALERT_LOG     "/tmp/fleet_alerts.log"
#define DEFAULT_PRICES_FILE   "config/fleet.cfg"

/* ── Driving quality thresholds (overridden by fleet.cfg) ───────────── */
#define HARSH_BRAKE_THRESHOLD   -4.0f   /* m/s²  */
#define HARSH_ACCEL_THRESHOLD    3.0f   /* m/s²  */
#define HARSH_TURN_THRESHOLD    45.0f   /* °/s   */
#define SPEED_LIMIT_KMH         60.0f   /* km/h  */

/* ── Score weights ───────────────────────────────────────────────────── */
#define PENALTY_HARSH_BRAKE    5
#define PENALTY_HARSH_ACCEL    3
#define PENALTY_HARSH_TURN     4
#define PENALTY_OVERSPEED      2
#define PENALTY_LATE_MIN       1

/* ─────────────────────────────────────────────────────────────────────
 * Telemetry frame — sent from STM32 to BBG via I2C
 * 80 bytes, packed (no padding)
 * MUST be identical on STM32 and BBG sides
 * ───────────────────────────────────────────────────────────────────── */
#pragma pack(push, 1)
typedef struct {
    /* Identity (32 bytes) */
    char     car_id[8];         /**< "CAR-001\0"     [0-7]   */
    char     driver_id[8];      /**< "DRV-042\0"     [8-15]  */
    char     trip_id[16];       /**< "T2026001\0"    [16-31] */

    /* GPS (12 bytes) */
    float    latitude;          /**< degrees         [32-35] */
    float    longitude;         /**< degrees         [36-39] */
    float    gps_speed_kmh;     /**< km/h from GPS   [40-43] */

    /* Accelerometer (12 bytes) */
    float    accel_x;           /**< m/s² forward    [44-47] */
    float    accel_y;           /**< m/s² lateral    [48-51] */
    float    accel_z;           /**< m/s² vertical   [52-55] */

    /* Gyroscope (12 bytes) */
    float    gyro_x;            /**< °/s pitch       [56-59] */
    float    gyro_y;            /**< °/s roll        [60-63] */
    float    gyro_z;            /**< °/s yaw/turn    [64-67] */

    /* Control (12 bytes) */
    uint32_t timestamp_sec;     /**< Unix timestamp  [68-71] */
    uint8_t  msg_type;          /**< 'S','D','E'     [72]    */
    uint8_t  gps_fix;           /**< 0=none, 1=fix   [73]    */
    uint8_t  satellites;        /**< count in view   [74]    */
    uint8_t  reserved[5];       /**< future use      [75-79] */
} telemetry_frame_t;            /**< total = 80 bytes        */
#pragma pack(pop)

/**
 * @brief Driving quality metrics derived from one telemetry frame.
 * Computed on the BBG before sending to the server.
 */
typedef struct {
    int   harsh_brake;    /**< 1 if accel_x < HARSH_BRAKE_THRESHOLD  */
    int   harsh_accel;    /**< 1 if accel_x > HARSH_ACCEL_THRESHOLD  */
    int   harsh_turn;     /**< 1 if abs(gyro_z) > HARSH_TURN_THRESHOLD*/
    int   overspeed;      /**< 1 if gps_speed_kmh > SPEED_LIMIT_KMH  */
} metrics_t;

#ifdef __cplusplus
}
#endif

#endif /* FLEET_COMMON_H */
