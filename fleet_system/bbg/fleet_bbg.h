/**
 * @file fleet_bbg.h
 * @brief Shared type definitions for the BBG fleet gateway.
 */

#ifndef FLEET_BBG_H
#define FLEET_BBG_H

#include <pthread.h>
#include "../common/common.h"

/**
 * @brief One slot per virtual car in the in-process shared structure.
 */
typedef struct {
    telemetry_frame_t  frame;
    int                has_data;
    pthread_mutex_t    mutex;
    pthread_cond_t     cond;
} car_slot_t;

/**
 * @brief Chronological ring buffer of all received frames across all cars.
 */
typedef struct {
    telemetry_frame_t  frames[RING_SIZE];
    int                head;
    int                count;
    pthread_mutex_t    mutex;
} ring_buffer_t;

/**
 * @brief All shared data between the I2C reader thread and 100 car threads.
 * Regular process memory — NOT shmget. All 101 threads share the same
 * address space so no shmget/shmat is needed.
 */
typedef struct {
    car_slot_t    latest[MAX_CARS];
    ring_buffer_t history;
} fleet_shared_t;

/**
 * @brief Arguments passed to each car thread at creation.
 * Heap-allocated before pthread_create(), freed by the car thread on exit.
 */
typedef struct {
    int         car_idx;
    const char *server_ip;
    int         server_port;
} car_thread_arg_t;

#endif /* FLEET_BBG_H */
