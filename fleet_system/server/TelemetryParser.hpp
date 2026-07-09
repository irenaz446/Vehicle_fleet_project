/**
 * @file TelemetryParser.hpp
 * @brief Parses fleet wire messages into structured telemetry samples.
 *
 * Wire format:
 *   "<TYPE>|<CAR>|<DRIVER>|<TRIP>|<LAT>|<LON>|<SPD>|
 *    <AX>|<AY>|<AZ>|<GX>|<GY>|<GZ>|<TS>|<HB>|<HA>|<HT>|<OS>\n"
 */

#pragma once

#include <string>
#include <optional>
#include <sstream>
#include "../common/common.h"

/** @brief Fully parsed telemetry sample used by server components. */
struct TelemetrySample {
    char     msg_type;
    std::string car_id;
    std::string driver_id;
    std::string trip_id;
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
    int      harsh_brake;
    int      harsh_accel;
    int      harsh_turn;
    int      overspeed;
};

class TelemetryParser {
public:
    /**
     * @brief Parse one wire line into a TelemetrySample.
     * @param line  Newline already stripped.
     * @return Populated sample, or std::nullopt on parse error.
     */
    static std::optional<TelemetrySample> parse(const std::string &line)
    {
        TelemetrySample s{};
        std::istringstream ss(line);
        std::string tok;

        /* TYPE */
        if (!std::getline(ss, tok, '|')) return std::nullopt;
        if (tok.size() != 1) return std::nullopt;
        s.msg_type = tok[0];
        if (s.msg_type != MSG_TRIP_START &&
            s.msg_type != MSG_TRIP_DATA  &&
            s.msg_type != MSG_TRIP_END)
            return std::nullopt;

        /* CAR_ID */
        if (!std::getline(ss, tok, '|')) return std::nullopt;
        s.car_id = tok;

        /* DRIVER_ID */
        if (!std::getline(ss, tok, '|')) return std::nullopt;
        s.driver_id = tok;

        /* TRIP_ID */
        if (!std::getline(ss, tok, '|')) return std::nullopt;
        s.trip_id = tok;

        try {
            /* LAT, LON, GPS_SPEED */
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.latitude = std::stof(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.longitude = std::stof(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.gps_speed_kmh = std::stof(tok);

            /* ACCEL X, Y, Z */
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.accel_x = std::stof(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.accel_y = std::stof(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.accel_z = std::stof(tok);

            /* GYRO X, Y, Z */
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.gyro_x = std::stof(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.gyro_y = std::stof(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.gyro_z = std::stof(tok);

            /* TIMESTAMP */
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.timestamp_sec = (uint32_t)std::stoul(tok);

            /* EVENT FLAGS */
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.harsh_brake = std::stoi(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.harsh_accel = std::stoi(tok);
            if (!std::getline(ss, tok, '|')) return std::nullopt;
            s.harsh_turn = std::stoi(tok);
            if (!std::getline(ss, tok)) return std::nullopt;
            s.overspeed = std::stoi(tok);
        }
        catch (...) { return std::nullopt; }

        return s;
    }
};
