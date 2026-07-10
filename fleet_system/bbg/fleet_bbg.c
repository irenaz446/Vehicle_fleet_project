/**
 * @file fleet_bbg.c
 * @brief BBG Vehicle Fleet Gateway — one process, 101 threads.
 *
 * Architecture:
 *   1 I2C reader thread  — reads 80-byte frames from STM32 via /dev/i2c-2
 *                          writes to fleet_shared_t (per-car slot + ring buffer)
 *
 *   100 car threads      — one per virtual car (CAR-001 to CAR-100)
 *                          each waits on its condvar for new data
 *                          computes driving metrics from raw sensor values
 *                          sends wire message to fleet server via its own TCP socket
 *
 * Shared memory:
 *   fleet_shared_t g_fleet — regular process memory, pthread-protected
 *   latest[100]            — per-car slot: mutex + condvar + latest frame
 *   history[1000]          — ring buffer of all frames in arrival order
 *
 * Compile: gcc fleet_bbg.c metrics.c -o fleet_bbg -lpthread -lm
 * Run:     sudo ./fleet_bbg <SERVER_IP> <SERVER_PORT>
 *          e.g. sudo ./fleet_bbg 192.168.10.1 9090
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <math.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/i2c-dev.h>

#include "../common/common.h"
#include "metrics.h"
#include "fleet_bbg.h"    /* car_slot_t, ring_buffer_t, fleet_shared_t, car_thread_arg_t */

/* ── Configuration ───────────────────────────────────────────────────── */
#define I2C_DEV           "/dev/i2c-2"
#define STM32_SLAVE_ADDR  0x08
#define FRAME_SIZE        84
#define RECONNECT_DELAY_S 3

/* ── Globals ─────────────────────────────────────────────────────────── */
static fleet_shared_t   g_fleet;
static volatile int     g_running = 1;
static const char      *g_server_ip;
static int              g_server_port;

/* ── Signal handler ──────────────────────────────────────────────────── */
static void handle_signal(int s) { (void)s; g_running = 0; }

/* ── Shared memory initialisation ───────────────────────────────────── */

/**
 * @brief Initialise all mutexes, condvars, and flags in g_fleet.
 *        Must be called once before any thread is started.
 */
static void fleet_shared_init(void)
{
    /* History ring buffer */
    pthread_mutex_init(&g_fleet.history.mutex, NULL);
    g_fleet.history.head  = 0;
    g_fleet.history.count = 0;

    /* Per-car slots */
    for (int i = 0; i < MAX_CARS; i++) {
        pthread_mutex_init(&g_fleet.latest[i].mutex, NULL);
        pthread_cond_init (&g_fleet.latest[i].cond,  NULL);
        g_fleet.latest[i].has_data = 0;
        memset(&g_fleet.latest[i].frame, 0, sizeof(telemetry_frame_t));
    }
    printf("[FLEET] Shared memory initialised (%d car slots, "
           "%d ring entries)\n", MAX_CARS, RING_SIZE);
}

/**
 * @brief Destroy all pthread primitives in g_fleet.
 *        Called at shutdown.
 */
static void fleet_shared_destroy(void)
{
    pthread_mutex_destroy(&g_fleet.history.mutex);
    for (int i = 0; i < MAX_CARS; i++) {
        pthread_mutex_destroy(&g_fleet.latest[i].mutex);
        pthread_cond_destroy (&g_fleet.latest[i].cond);
    }
}

/* ── I2C helpers ─────────────────────────────────────────────────────── */

static int i2c_open(void)
{
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        printf("[ERR] Cannot open %s: %s\n", I2C_DEV, strerror(errno));
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, STM32_SLAVE_ADDR) < 0) {
        printf("[ERR] ioctl I2C_SLAVE: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("[I2C] Opened %s, slave=0x%02X, frame=%d bytes\n",
           I2C_DEV, STM32_SLAVE_ADDR, FRAME_SIZE);

    /* Verify frame size matches the struct */
    if ((int)sizeof(telemetry_frame_t) != FRAME_SIZE) {
        printf("[ERR] sizeof(telemetry_frame_t)=%zu but FRAME_SIZE=%d — mismatch!\n",
               sizeof(telemetry_frame_t), FRAME_SIZE);
        close(fd);
        return -1;
    }
    printf("[I2C] sizeof(telemetry_frame_t)=%zu OK\n",
           sizeof(telemetry_frame_t));

    return fd;
}

/**
 * @brief Extract the car index (0-99) from a car_id string.
 *        "CAR-001" → 0, "CAR-042" → 41, "CAR-100" → 99
 * @return Index 0-99, or -1 if invalid.
 */
static int car_id_to_idx(const char *car_id)
{
    /* Expect format "CAR-NNN" */
    int num = 0;
    if (sscanf(car_id, "CAR-%d", &num) != 1) return -1;
    if (num < 1 || num > MAX_CARS)           return -1;
    return num - 1;
}

/* ── I2C Reader Thread ───────────────────────────────────────────────── */

/**
 * @brief Reads telemetry frames from STM32 and writes to g_fleet.
 *
 * For each received frame:
 *   1. Writes to latest[car_idx] and signals the car's condvar.
 *   2. Writes to history ring buffer.
 */
static void *i2c_reader_thread(void *arg)
{
    (void)arg;

    int fd = i2c_open();
    if (fd < 0) {
        printf("[ERR] I2C reader: failed to open bus\n");
        g_running = 0;
        return NULL;
    }

    printf("[I2C] Reader thread started\n");
    long frames_received = 0;

    while (g_running) {
        unsigned char buf[FRAME_SIZE];
        memset(buf, 0, sizeof(buf));

        /* Blocking read — waits until STM32 sends next frame */
        int n = read(fd, buf, FRAME_SIZE);
        if (n < 0) {
            printf("[ERR] I2C read: %s\n", strerror(errno));
            sleep(1);
            continue;
        }
        if (n < FRAME_SIZE) {
            printf("[WARN] Short read: %d/%d bytes\n", n, FRAME_SIZE);
            continue;
        }

        telemetry_frame_t *f = (telemetry_frame_t *)buf;

        /* Force null-termination on string fields */
        f->car_id[7]     = '\0';
        f->driver_id[7]  = '\0';
        f->trip_id[15]   = '\0';

        /* Validate message type */
        if (f->msg_type != MSG_TRIP_START &&
            f->msg_type != MSG_TRIP_DATA  &&
            f->msg_type != MSG_TRIP_END) {
            printf("[WARN] Bad msg_type: 0x%02X  raw bytes[0..7]: "
                   "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                   (unsigned char)f->msg_type,
                   buf[0], buf[1], buf[2], buf[3],
                   buf[4], buf[5], buf[6], buf[7]);
            continue;
        }

        int idx = car_id_to_idx(f->car_id);
        if (idx < 0) {
            printf("[WARN] Unknown car_id: %s\n", f->car_id);
            continue;
        }

        frames_received++;

        /* ── Write to per-car slot, signal car thread ── */
        pthread_mutex_lock(&g_fleet.latest[idx].mutex);
        g_fleet.latest[idx].frame    = *(telemetry_frame_t *)buf;
        g_fleet.latest[idx].has_data = 1;
        pthread_cond_signal(&g_fleet.latest[idx].cond);
        pthread_mutex_unlock(&g_fleet.latest[idx].mutex);

        /* ── Write to history ring buffer ── */
        pthread_mutex_lock(&g_fleet.history.mutex);
        int pos = g_fleet.history.head % RING_SIZE;
        g_fleet.history.frames[pos] = *(telemetry_frame_t *)buf;
        g_fleet.history.head++;
        g_fleet.history.count++;
        pthread_mutex_unlock(&g_fleet.history.mutex);

        /* Progress report every 100 frames */
        if (frames_received % 100 == 0)
            printf("[I2C] %ld frames received\n", frames_received);
    }

    close(fd);
    printf("[I2C] Reader thread stopped\n");
    return NULL;
}

/* ── TCP helpers ─────────────────────────────────────────────────────── */

static int tcp_connect(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ── Car Thread ──────────────────────────────────────────────────────── */

/**
 * @brief One car thread — represents one virtual vehicle.
 *
 * Flow:
 *   1. Connect to fleet server via TCP.
 *   2. Wait on condvar until I2C reader signals new data for this car.
 *   3. Copy the frame under lock.
 *   4. Compute driving metrics.
 *   5. Format and send wire message.
 *   6. Read server reply.
 *   7. Repeat.
 */
static void *car_thread(void *arg)
{
    car_thread_arg_t *a   = (car_thread_arg_t *)arg;
    int               idx = a->car_idx;
    car_slot_t       *slot = &g_fleet.latest[idx];

    /* Each car thread has its own TCP socket — no lock needed on send */
    int tcp_fd = -1;

    char car_label[16];
    snprintf(car_label, sizeof(car_label), "CAR-%03d", idx + 1);

    printf("[CAR] Thread started for %s\n", car_label);

    while (g_running) {

        /* ── 1. Ensure TCP connection ── */
        if (tcp_fd < 0) {
            tcp_fd = tcp_connect(a->server_ip, a->server_port);
            if (tcp_fd < 0) {
                sleep(RECONNECT_DELAY_S);
                continue;
            }
            printf("[%s] TCP connected\n", car_label);
        }

        /* ── 2. Wait for new data from I2C reader ── */
        pthread_mutex_lock(&slot->mutex);
        while (!slot->has_data && g_running)
            pthread_cond_wait(&slot->cond, &slot->mutex);

        if (!g_running) {
            pthread_mutex_unlock(&slot->mutex);
            break;
        }

        /* ── 3. Copy frame and clear flag ── */
        telemetry_frame_t frame = slot->frame;
        slot->has_data = 0;
        pthread_mutex_unlock(&slot->mutex);

        /* ── 4. Compute metrics ── */
        metrics_t m = compute_metrics(&frame);

        /* ── 5. Format wire message ── */
        /*
         * Format:
         * "<TYPE>|<CAR>|<DRIVER>|<TRIP>|<LAT>|<LON>|<SPD>|
         *  <AX>|<AY>|<AZ>|<GX>|<GY>|<GZ>|<TS>|
         *  <HB>|<HA>|<HT>|<OS>\n"
         */
        char wire[BUF_SIZE];
        int  wlen = snprintf(wire, sizeof(wire),
            "%c|%s|%s|%s|%.4f|%.4f|%.2f|"
            "%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%u|"
            "%d|%d|%d|%d\n",
            frame.msg_type,
            frame.car_id,
            frame.driver_id,
            frame.trip_id,
            frame.latitude,
            frame.longitude,
            frame.gps_speed_kmh,
            frame.accel_x,
            frame.accel_y,
            frame.accel_z,
            frame.gyro_x,
            frame.gyro_y,
            frame.gyro_z,
            frame.timestamp_sec,
            m.harsh_brake,
            m.harsh_accel,
            m.harsh_turn,
            m.overspeed);

        /* ── 6. Send to server ── */
        if (send(tcp_fd, wire, (size_t)wlen, MSG_NOSIGNAL) < 0) {
            printf("[%s] TCP send failed — reconnecting\n", car_label);
            close(tcp_fd);
            tcp_fd = -1;
            continue;
        }

        /* ── 7. Read server reply (non-blocking) ── */
        char reply[128];
        ssize_t n = recv(tcp_fd, reply, sizeof(reply) - 1, MSG_DONTWAIT);
        if (n > 0) {
            reply[n] = '\0';
            reply[strcspn(reply, "\r\n")] = '\0';
            /* Only print non-trivial replies (not ACK:ok spam) */
            if (frame.msg_type == MSG_TRIP_END)
                printf("[%s] %s\n", car_label, reply);
        }
    }

    if (tcp_fd >= 0) close(tcp_fd);
    printf("[CAR] Thread stopped for %s\n", car_label);
    free(a);
    return NULL;
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <SERVER_IP> <SERVER_PORT>\n", argv[0]);
        printf("  e.g. %s 192.168.10.1 9090\n", argv[0]);
        return 1;
    }

    g_server_ip   = argv[1];
    g_server_port = atoi(argv[2]);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    printf("=== Vehicle Fleet BBG Gateway starting ===\n");
    printf("Server : %s:%d\n", g_server_ip, g_server_port);
    printf("Cars   : %d virtual vehicles\n", MAX_CARS);
    printf("Threads: 1 I2C reader + %d car threads\n\n", MAX_CARS);

    /* Initialise shared memory */
    fleet_shared_init();

    pthread_t threads[MAX_CARS + 1];

    /* Start I2C reader thread */
    pthread_create(&threads[0], NULL, i2c_reader_thread, NULL);

    /* Start 100 car threads */
    for (int i = 0; i < MAX_CARS; i++) {
        car_thread_arg_t *arg = malloc(sizeof(car_thread_arg_t));
        arg->car_idx    = i;
        arg->server_ip  = g_server_ip;
        arg->server_port = g_server_port;
        pthread_create(&threads[i + 1], NULL, car_thread, arg);
    }

    printf("[FLEET] All %d threads started\n\n", MAX_CARS + 1);

    /* Wait for all threads to finish */
    for (int i = 0; i <= MAX_CARS; i++)
        pthread_join(threads[i], NULL);

    fleet_shared_destroy();
    printf("[FLEET] Shutdown complete\n");
    return 0;
}
