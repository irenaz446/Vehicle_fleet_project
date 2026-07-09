/**
 * @file server_main.cpp
 * @brief Vehicle Fleet Management Server — entry point.
 *
 * Three threads in one process:
 *   1. TCP thread   — poll()-based event loop, receives telemetry from 100 BBG cars
 *                     parses messages → TripManager → on END pushes to TripQueue
 *                     ViolationDetector checks every sample
 *   2. DB thread    — pops CompletedTrip from TripQueue → writes to SQLite
 *   3. Main thread  — starts the other two, waits for shutdown signal
 *
 * No POSIX shared memory (shmget) — TripQueue is a std::queue + std::mutex.
 *
 * Configuration (config/fleet.cfg):
 *   PORT        TCP listen port         (default 9090)
 *   DB_PATH     SQLite file             (default fleet.db)
 *   LOG_FILE    Server log              (default /tmp/fleet_logs/server.log)
 *   ALERT_LOG   Violation alert log     (default /tmp/fleet_alerts.log)
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <cstring>
#include <cerrno>
#include <vector>
#include <unordered_map>
#include <memory>

#include "../common/common.h"
#include "../common/Logger.hpp"
#include "../common/Config.hpp"
#include "TelemetryParser.hpp"
#include "TripManager.hpp"
#include "TripQueue.hpp"
#include "ViolationDetector.hpp"
#include "Database.hpp"

/* ── Global shutdown flag ────────────────────────────────────────────── */
static std::atomic<bool> g_running{true};
static void sighandler(int) { g_running = false; }

/* ── Shared between TCP thread and DB thread ─────────────────────────── */
static TripQueue g_trip_queue;

/* ── DB Thread ───────────────────────────────────────────────────────── */

void db_thread_func(const std::string &db_path)
{
    Logger::info("DB thread started");
    std::unique_ptr<Database> db;
    try {
        db = std::make_unique<Database>(db_path);
    } catch (const std::exception &e) {
        Logger::error(std::string("DB open failed: ") + e.what());
        return;
    }

    while (g_running || g_trip_queue.size() > 0) {
        auto trip = g_trip_queue.pop(g_running.load());
        if (!trip.has_value()) break;
        db->insertTrip(*trip);
    }

    Logger::info("DB thread stopped");
}

/* ── TCP Thread ──────────────────────────────────────────────────────── */

void tcp_thread_func(int port,
                     const std::string &alert_log)
{
    Logger::info("TCP thread started on port " + std::to_string(port));

    /* Violation detector — writes to alert log in real time */
    ViolationDetector detector(alert_log);

    /* Trip manager — tracks all 100 active trips */
    TripManager trip_manager;

    /* Set up listening socket */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        Logger::error("socket(): " + std::string(strerror(errno)));
        return;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(listen_fd, (sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(listen_fd, SOMAXCONN) < 0) {
        Logger::error("bind/listen: " + std::string(strerror(errno)));
        close(listen_fd);
        return;
    }

    /* poll() fd array */
    std::vector<pollfd>              fds;
    std::vector<std::string>         recv_bufs;
    std::unordered_map<int,std::string> fd_to_car;

    fds.push_back({listen_fd, POLLIN, 0});
    recv_bufs.push_back("");

    Logger::info("Waiting for BBG connections...");

    long total_samples = 0;
    long total_trips   = 0;

    while (g_running) {
        int ret = poll(fds.data(), (nfds_t)fds.size(), 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* New connection */
        if (fds[0].revents & POLLIN) {
            sockaddr_in caddr{};
            socklen_t clen = sizeof(caddr);
            int cfd = accept(listen_fd, (sockaddr *)&caddr, &clen);
            if (cfd >= 0 && fds.size() < MAX_CLIENTS + 1) {
                fds.push_back({cfd, POLLIN, 0});
                recv_bufs.push_back("");
                Logger::info("Car connected from " +
                             std::string(inet_ntoa(caddr.sin_addr)));
            } else if (cfd >= 0) {
                close(cfd);
            }
        }

        /* Existing clients */
        for (int i = (int)fds.size() - 1; i >= 1; --i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;

            char buf[BUF_SIZE];
            ssize_t n = recv(fds[i].fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                close(fds[i].fd);
                fds.erase(fds.begin() + i);
                recv_bufs.erase(recv_bufs.begin() + i);
                continue;
            }

            buf[n] = '\0';
            recv_bufs[i] += buf;

            /* Process complete lines */
            std::string &rbuf = recv_bufs[i];
            std::size_t  pos;
            while ((pos = rbuf.find('\n')) != std::string::npos) {
                std::string line = rbuf.substr(0, pos);
                rbuf.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty()) continue;

                auto sample = TelemetryParser::parse(line);
                if (!sample.has_value()) {
                    send(fds[i].fd, "ERR:bad_msg\n", 12, MSG_NOSIGNAL);
                    continue;
                }

                total_samples++;

                /* Check for violations on every data sample */
                if (sample->msg_type == MSG_TRIP_DATA)
                    detector.check(*sample);

                /* Feed to trip manager */
                auto completed = trip_manager.process(*sample);
                if (completed.has_value()) {
                    total_trips++;
                    Logger::info("Trip complete: " + completed->trip_id +
                                 " car=" + completed->car_id +
                                 " score=" + std::to_string(completed->score) +
                                 " (" + completed->grade + ")");

                    /* Push to DB thread via queue (no shmget!) */
                    g_trip_queue.push(*completed);

                    /* Reply to BBG */
                    std::string reply = "OK:trip=" + completed->trip_id +
                                        ":score=" +
                                        std::to_string(completed->score) +
                                        ":grade=" + completed->grade + "\n";
                    send(fds[i].fd, reply.c_str(), reply.size(),
                         MSG_NOSIGNAL);
                } else {
                    send(fds[i].fd, "ACK:ok\n", 7, MSG_NOSIGNAL);
                }

                /* Periodic status log */
                if (total_samples % 1000 == 0) {
                    Logger::info("Status: samples=" +
                                 std::to_string(total_samples) +
                                 " trips=" + std::to_string(total_trips) +
                                 " active=" +
                                 std::to_string(trip_manager.activeCount()) +
                                 " connections=" +
                                 std::to_string(fds.size() - 1));
                }
            }
        }
    }

    for (auto &f : fds) close(f.fd);
    Logger::info("TCP thread stopped. Total samples=" +
                 std::to_string(total_samples) +
                 " trips=" + std::to_string(total_trips));
}

/* ── Main ────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/fleet.cfg";
    Config cfg(cfg_path);

    const int         port      = cfg.getInt("PORT",      SERVER_PORT);
    const std::string db_path   = cfg.get("DB_PATH",      DEFAULT_DB_PATH);
    const std::string log_file  = cfg.get("LOG_FILE",     "/tmp/fleet_logs/server.log");
    const std::string alert_log = cfg.get("ALERT_LOG",    DEFAULT_ALERT_LOG);

    mkdir(DEFAULT_LOG_DIR, 0755);
    Logger::init(log_file);

    std::cout << "=== Vehicle Fleet Management Server starting ===\n";
    std::cout << "Port      : " << port      << "\n";
    std::cout << "Database  : " << db_path   << "\n";
    std::cout << "Alert log : " << alert_log << "\n\n";
    Logger::info("=== Vehicle Fleet Management Server starting ===");

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    /* Start DB thread */
    std::thread db_thread(db_thread_func, db_path);

    /* Start TCP thread */
    std::thread tcp_thread(tcp_thread_func, port, alert_log);

    /* Main thread waits for shutdown */
    tcp_thread.join();

    /* Signal DB thread to flush and exit */
    g_trip_queue.push({}); /* wake it up if blocked */
    db_thread.join();

    Logger::info("Vehicle Fleet server shut down cleanly");
    Logger::close();
    return 0;
}
