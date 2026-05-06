/**
 * @file cmb_traffic_sink.h
 * @brief Traffic sink that receives packets and collects statistics.
 *
 * A traffic sink is a process that receives packets from a switch
 * and collects statistics such as latency, jitter, and loss.
 */

/*
 * Copyright (c) Kevin J. M. 2025-26.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef CIMBA_CMB_TRAFFIC_SINK_H
#define CIMBA_CMB_TRAFFIC_SINK_H

#include <stdint.h>
#include <stdbool.h>

#include "cmb_process.h"
#include "cmb_packet.h"
#include "cmb_datasummary.h"

#define CMB_QOS_LEVELS 5

struct cmb_qos_stats {
    uint64_t packets_received;
    uint64_t bytes_received;
    double total_delay_ns;
    double min_delay_ns;
    double max_delay_ns;
    uint64_t ce_marked_count;
};

/**
 * @brief Statistics collected by a traffic sink.
 */
struct cmb_traffic_sink_stats {
    uint64_t packets_received;
    uint64_t bytes_received;
    double total_delay_ns;
    double min_delay_ns;
    double max_delay_ns;
    struct cmb_datasummary delay_summary;

    struct cmb_qos_stats qos_stats[CMB_QOS_LEVELS];
};

/**
 * @brief A traffic sink process.
 *
 * Receives packets and collects statistics on them.
 */
struct cmb_traffic_sink {
    struct cmb_process proc;
    struct cmb_nodeswitch *src_switch;
    uint32_t listen_addr;
    bool running;
    struct cmb_traffic_sink_stats stats;
};

/**
 * @brief Create a new traffic sink.
 *
 * @return Pointer to the newly created sink.
 */
extern struct cmb_traffic_sink *cmb_traffic_sink_create(void);

/**
 * @brief Initialize a traffic sink.
 *
 * @param ts Pointer to an allocated sink.
 * @param name Name for the sink process.
 * @param src_switch Pointer to the switch to receive from.
 * @param listen_addr Address to listen for (0 = all).
 */
extern void cmb_traffic_sink_initialize(struct cmb_traffic_sink *ts,
                                         const char *name,
                                         struct cmb_nodeswitch *src_switch,
                                         uint32_t listen_addr);

/**
 * @brief Uninitialize a traffic sink.
 *
 * @param ts Pointer to a sink.
 */
extern void cmb_traffic_sink_terminate(struct cmb_traffic_sink *ts);

/**
 * @brief Free memory for a traffic sink.
 *
 * @param ts Pointer to a sink.
 */
extern void cmb_traffic_sink_destroy(struct cmb_traffic_sink *ts);

/**
 * @brief Start the traffic sink.
 *
 * @param ts Pointer to a sink.
 */
extern void cmb_traffic_sink_start(struct cmb_traffic_sink *ts);

/**
 * @brief Stop the traffic sink.
 *
 * @param ts Pointer to a sink.
 */
extern void cmb_traffic_sink_stop(struct cmb_traffic_sink *ts);

/**
 * @brief Get statistics from the sink.
 *
 * @param ts Pointer to a sink.
 * @return Pointer to the statistics.
 */
static inline const struct cmb_traffic_sink_stats *cmb_traffic_sink_get_stats(
    const struct cmb_traffic_sink *ts)
{
    return &ts->stats;
}

/**
 * @brief Get packets received count.
 *
 * @param ts Pointer to a sink.
 * @return Number of packets received.
 */
static inline uint64_t cmb_traffic_sink_packets_received(const struct cmb_traffic_sink *ts)
{
    return ts->stats.packets_received;
}

/**
 * @brief Get average delay in nanoseconds.
 *
 * @param ts Pointer to a sink.
 * @return Average delay, or 0 if no packets.
 */
static inline double cmb_traffic_sink_avg_delay_ns(const struct cmb_traffic_sink *ts)
{
    if (ts->stats.packets_received == 0) {
        return 0.0;
    }
    return ts->stats.total_delay_ns / (double)ts->stats.packets_received;
}

static inline double cmb_traffic_sink_min_delay_ns(const struct cmb_traffic_sink *ts)
{
    return ts->stats.min_delay_ns;
}

static inline double cmb_traffic_sink_max_delay_ns(const struct cmb_traffic_sink *ts)
{
    return ts->stats.max_delay_ns;
}

static inline uint64_t cmb_traffic_sink_bytes_received(const struct cmb_traffic_sink *ts)
{
    return ts->stats.bytes_received;
}

static inline double cmb_traffic_sink_total_delay_ns(const struct cmb_traffic_sink *ts)
{
    return ts->stats.total_delay_ns;
}

/**
 * @brief Check if sink is running.
 *
 * @param ts Pointer to a sink.
 * @return True if running.
 */
static inline bool cmb_traffic_sink_is_running(const struct cmb_traffic_sink *ts)
{
    return ts->running;
}

/**
 * @brief Print sink statistics.
 *
 * @param ts Pointer to a sink.
 * @param fp File pointer (e.g., stdout).
 */
extern void cmb_traffic_sink_print_stats(const struct cmb_traffic_sink *ts, FILE *fp);

#endif /* CIMBA_CMB_TRAFFIC_SINK_H */