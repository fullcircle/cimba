/**
 * @file cmb_network.h
 * @brief Network container for holding switches, links, and managing simulation.
 *
 * A network is a container that holds all nodeswitches and links in a
 * simulation. It provides functions to add components, run the simulation,
 * and collect statistics.
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
#ifndef CIMBA_CMB_NETWORK_H
#define CIMBA_CMB_NETWORK_H

#include <stdint.h>
#include <stdbool.h>

#include "cmb_nodeswitch.h"
#include "cmb_link.h"
#include "cmb_random.h"

struct cmb_traffic_sink;

#define CMB_NETWORK_MAX_SWITCHES 64
#define CMB_NETWORK_MAX_LINKS 128
#define CMB_NETWORK_MAX_TRAFFIC_SINKS 32
#define CMB_NETWORK_MAX_TRAFFIC_SINKS 32

/**
 * @brief Statistics collection level for the network.
 */
enum cmb_network_stats_level {
    CMB_NETWORK_STATS_NONE = 0,
    CMB_NETWORK_STATS_AGGREGATE = 1,
    CMB_NETWORK_STATS_DETAILED = 2
};

/**
 * @brief A network container holding all simulation components.
 *
 * The network manages all switches, links, and provides a unified interface
 * for running simulations and collecting statistics.
 */
struct cmb_network {
    struct cmb_nodeswitch *switches[CMB_NETWORK_MAX_SWITCHES];
    uint16_t switch_count;

    struct cmb_link *links[CMB_NETWORK_MAX_LINKS];
    uint16_t link_count;

    struct cmb_random *rng;

    enum cmb_network_stats_level stats_level;

    struct {
        uint64_t total_packets_created;
        uint64_t total_packets_transmitted;
        uint64_t total_packets_dropped;
        uint64_t total_packets_delivered;
        double total_delay_ns;
        double total_queue_time_ns;
    } stats;

    struct cmb_process **tx_workers;
    uint16_t tx_worker_count;

    struct cmb_traffic_sink *traffic_sinks[CMB_NETWORK_MAX_TRAFFIC_SINKS];
    uint16_t traffic_sink_count;
};

/**
 * @brief Configuration for creating a network.
 */
struct cmb_network_config {
    enum cmb_network_stats_level stats_level;
    uint64_t rng_seed;
};

/**
 * @brief Create a new network.
 *
 * @return Pointer to the newly created network.
 */
extern struct cmb_network *cmb_network_create(void);

/**
 * @brief Initialize a network for use.
 *
 * @param net Pointer to an already allocated network.
 * @param config Network configuration (can be NULL for defaults).
 */
extern void cmb_network_initialize(struct cmb_network *net,
                                     const struct cmb_network_config *config);

/**
 * @brief Uninitialize a network.
 *
 * @param net Pointer to a network.
 */
extern void cmb_network_terminate(struct cmb_network *net);

/**
 * @brief Free memory for a network.
 *
 * @param net Pointer to a network.
 */
extern void cmb_network_destroy(struct cmb_network *net);

/**
 * @brief Add a switch to the network.
 *
 * @param net Pointer to the network.
 * @param sw Pointer to the switch to add.
 * @return True if successful, false if network is full.
 */
extern bool cmb_network_add_switch(struct cmb_network *net, struct cmb_nodeswitch *sw);

/**
 * @brief Add a link to the network.
 *
 * @param net Pointer to the network.
 * @param link Pointer to the link to add.
 * @return True if successful, false if network is full.
 */
extern bool cmb_network_add_link(struct cmb_network *net, struct cmb_link *link);

/**
 * @brief Add a traffic sink to the network.
 *
 * @param net Pointer to the network.
 * @param ts Pointer to the traffic sink to add.
 * @return True if successful, false if network is full.
 */
extern bool cmb_network_add_traffic_sink(struct cmb_network *net, struct cmb_traffic_sink *ts);

/**
 * @brief Start all switches and transmit workers in the network.
 *
 * @param net Pointer to the network.
 */
extern void cmb_network_start(struct cmb_network *net);

/**
 * @brief Stop all switches and transmit workers in the network.
 *
 * @param net Pointer to the network.
 */
extern void cmb_network_stop(struct cmb_network *net);

/**
 * @brief Run the simulation for a specified duration.
 *
 * @param net Pointer to the network.
 * @param duration Duration in simulated seconds.
 */
extern void cmb_network_run(struct cmb_network *net, double duration);

/**
 * @brief Get a switch by index.
 *
 * @param net Pointer to the network.
 * @param index Switch index (0-based).
 * @return Pointer to the switch, or NULL if invalid index.
 */
extern struct cmb_nodeswitch *cmb_network_get_switch(const struct cmb_network *net, uint16_t index);

/**
 * @brief Get a link by index.
 *
 * @param net Pointer to the network.
 * @param index Link index (0-based).
 * @return Pointer to the link, or NULL if invalid index.
 */
extern struct cmb_link *cmb_network_get_link(const struct cmb_network *net, uint16_t index);

/**
 * @brief Get total packets transmitted across all links.
 *
 * @param net Pointer to the network.
 * @return Total packets transmitted.
 */
extern uint64_t cmb_network_packets_transmitted(const struct cmb_network *net);

/**
 * @brief Get total packets dropped across all switches and links.
 *
 * @param net Pointer to the network.
 * @return Total packets dropped.
 */
extern uint64_t cmb_network_packets_dropped(const struct cmb_network *net);

/**
 * @brief Get total packets delivered (reached destination).
 *
 * @param net Pointer to the network.
 * @return Total packets delivered.
 */
extern uint64_t cmb_network_packets_delivered(const struct cmb_network *net);

/**
 * @brief Get average packet delay in nanoseconds.
 *
 * @param net Pointer to the network.
 * @return Average delay, or 0 if no packets.
 */
extern double cmb_network_avg_delay_ns(const struct cmb_network *net);

/**
 * @brief Get total CE-marked packets across all switches.
 *
 * @param net Pointer to the network.
 * @return Total packets marked with CE.
 */
extern uint64_t cmb_network_total_ce_marked(const struct cmb_network *net);

/**
 * @brief Print network statistics.
 *
 * @param net Pointer to the network.
 * @param fp File pointer (e.g., stdout).
 */
extern void cmb_network_print_stats(const struct cmb_network *net, FILE *fp);

#endif /* CIMBA_CMB_NETWORK_H */