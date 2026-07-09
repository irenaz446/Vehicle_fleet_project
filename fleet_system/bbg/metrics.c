/**
 * @file metrics.c
 * @brief Compute driving quality metrics from a raw telemetry frame.
 *
 * Called by each car thread on the BBG before sending to the server.
 * Lightweight computation — no memory allocation, no blocking calls.
 */

#include "metrics.h"
#include "../common/common.h"
#include <math.h>

metrics_t compute_metrics(const telemetry_frame_t *frame)
{
    metrics_t m = {0, 0, 0, 0};

    /* Harsh braking: large negative forward acceleration */
    if (frame->accel_x < HARSH_BRAKE_THRESHOLD)
        m.harsh_brake = 1;

    /* Harsh acceleration: large positive forward acceleration */
    if (frame->accel_x > HARSH_ACCEL_THRESHOLD)
        m.harsh_accel = 1;

    /* Harsh turn: high yaw angular velocity (left or right) */
    if (fabsf(frame->gyro_z) > HARSH_TURN_THRESHOLD)
        m.harsh_turn = 1;

    /* Overspeed: GPS speed above city limit */
    if (frame->gps_speed_kmh > SPEED_LIMIT_KMH)
        m.overspeed = 1;

    return m;
}
