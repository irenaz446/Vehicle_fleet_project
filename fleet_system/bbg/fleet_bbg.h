/**
 * @file fleet_bbg.h
 * @brief Shared type definitions for the BBG fleet gateway.
 *
 * Separating structs into a header allows other translation units
 * (e.g. diagnostic tools, test harnesses) to include these types
 * without pulling in the full implementation.
 */

#ifndef FLEET_BBG_H
#define FLEET_BBG_H

#include <pthread.h>
#include "../common/common.h"

/* ── Per-car slot in shared memory ──────────────────────────────────── */

/**
 * @brief One slot per virtual car in the in-process shared structure.
 *
 * The I2C reader thread writes to frame and sets has_data, then signals
 * the condition variable. The car thread waits on the condvar, reads
 * the frame, and clears has_data.
 */
typedef struct {
    telemetry_frame_t  frame;      /**< latest frame for this car          */
    int                has_data;   /**< 1 = new data not yet consumed      */
    pthread_mutex_t    mutex;      /**< protects frame and has_data        */
    pthread_cond_t     cond;       /**< car thread blocks here for data    */
} car_slot_t;

/* ── History ring buffer ─────────────────────────────────────────────── */

/**
 * @brief Chronological ring buffer of all received frames across all cars.
 *
 * Useful for diagnostics and offline replay.
 * head wraps at RING_SIZE — oldest frames are overwritten silently.
 */
typedef struct {
    telemetry_frame_t  frames[RING_SIZE]; /**< circular frame store        */
    int                head;              /**< next write index            */
    int                count;             /**< total frames ever written   */
    pthread_mutex_t    mutex;             /**< protects head and count     */
} ring_buffer_t;

/* ── Complete in-process shared structure ────────────────────────────── */

/**
 * @brief All shared data between the I2C reader thread and 100 car threads.
 *
 * This is NOT POSIX shmget — it is ordinary process memory. All 101
 * threads share the same address space so no shmget/shmat is needed.
 * Synchronisation is entirely via pthread primitives embedded in each slot.
 */
typedef struct {
    car_slot_t    latest[MAX_CARS]; /**< one slot per virtual car          */
    ring_buffer_t history;          /**< chronological history ring buffer */
} fleet_shared_t;

/* ── Thread argument ─────────────────────────────────────────────────── */

/**
 * @brief Arguments passed to each car thread at creation.
 *
 * Heap-allocated before pthread_create(), freed by the car thread itself
 * on exit so there is no dangling pointer.
 */
typedef struct {
    int         car_idx;     /**< which car this thread owns (0 to MAX_CARS-1) */
    const char *server_ip;   /**< fleet server IP address                       */
    int         server_port; /**< fleet server TCP port                         */
} car_thread_arg_t;

#endif /* FLEET_BBG_H */
