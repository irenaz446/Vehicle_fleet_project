/**
 * @file metrics.h
 * @brief Driving metrics computation interface.
 */

#ifndef FLEET_METRICS_H
#define FLEET_METRICS_H

#include "../common/common.h"

/**
 * @brief Compute driving quality metrics from one telemetry frame.
 * @param frame  Pointer to the received telemetry frame.
 * @return       metrics_t struct with binary event flags.
 */
metrics_t compute_metrics(const telemetry_frame_t *frame);

#endif /* FLEET_METRICS_H */
