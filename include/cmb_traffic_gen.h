/**
 * @file cmb_traffic_gen.h
 * @brief Traffic generator that creates packets at a specified rate.
 *
 * A traffic generator is a process that creates packets according to a
 * traffic pattern (constant rate, Poisson, bursty, etc.) and injects
 * them into the network at a specified switch and port.
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
#ifndef CIMBA_CMB_TRAFFIC_GEN_H
#define CIMBA_CMB_TRAFFIC_GEN_H

#include <stdint.h>
#include <stdbool.h>

#include "cmb_process.h"
#include "cmb_packet.h"
#include "cmb_random.h"

/**
 * @brief Traffic generation pattern.
 */
enum cmb_traffic_pattern {
    CMB_TRAFFIC_CONSTANT = 0,
    CMB_TRAFFIC_POISSON = 1,
    CMB_TRAFFIC_BURSTY = 2
};

/**
 * @brief Configuration for a traffic generator.
 */
struct cmb_traffic_gen_config {
    uint32_t src_addr;
    uint32_t dst_addr;
    double packet_rate_hz;
    uint32_t packet_size_bits;
    enum cmb_traffic_pattern pattern;
    enum cmb_qos_level qos;
    uint16_t vlan_id;
    uint8_t vlan_pcp;
    uint64_t num_packets;
    double burst_duration_sec;
    double burst_scale;
    bool enable_ecn_feedback;
    double ecn_feedback_threshold;
};

/**
 * @brief A traffic generator process.
 *
 * Generates packets according to a configured pattern and injects
 * them into the network at the specified switch.
 */
struct cmb_traffic_gen {
    struct cmb_process proc;
    struct cmb_nodeswitch *dst_switch;
    struct cmb_random *rng;
    struct cmb_traffic_gen_config config;
    uint64_t packets_generated;
    bool running;

    struct cmb_network *net;
    double congestion_factor;
    uint64_t last_ce_count;
    uint64_t packets_since_feedback;
};

/**
 * @brief Create a new traffic generator.
 *
 * @return Pointer to the newly created generator.
 */
extern struct cmb_traffic_gen *cmb_traffic_gen_create(void);

/**
 * @brief Initialize a traffic generator.
 *
 * @param tg Pointer to an allocated generator.
 * @param name Name for the generator process.
 * @param dst_switch Pointer to the destination switch.
 * @param config Generator configuration.
 * @param rng Random number generator to use (can be NULL for default).
 */
extern void cmb_traffic_gen_initialize(struct cmb_traffic_gen *tg,
                                       const char *name,
                                       struct cmb_nodeswitch *dst_switch,
                                       const struct cmb_traffic_gen_config *config,
                                       struct cmb_random *rng);

/**
 * @brief Uninitialize a traffic generator.
 *
 * @param tg Pointer to a generator.
 */
extern void cmb_traffic_gen_terminate(struct cmb_traffic_gen *tg);

/**
 * @brief Free memory for a traffic generator.
 *
 * @param tg Pointer to a generator.
 */
extern void cmb_traffic_gen_destroy(struct cmb_traffic_gen *tg);

/**
 * @brief Set the network for ECN feedback.
 *
 * When ECN feedback is enabled, the generator will query the network
 * for CE-marked packet counts and reduce rate accordingly.
 *
 * @param tg Pointer to a generator.
 * @param net Pointer to the network.
 */
extern void cmb_traffic_gen_set_network(struct cmb_traffic_gen *tg, struct cmb_network *net);

/**
 * @brief Get current congestion factor.
 *
 * @param tg Pointer to a generator.
 * @return Congestion factor (1.0 = no congestion, >1 = backed off).
 */
extern double cmb_traffic_gen_get_congestion_factor(const struct cmb_traffic_gen *tg);

/**
 * @brief Start the traffic generator.
 *
 * @param tg Pointer to a generator.
 */
extern void cmb_traffic_gen_start(struct cmb_traffic_gen *tg);

/**
 * @brief Stop the traffic generator.
 *
 * @param tg Pointer to a generator.
 */
extern void cmb_traffic_gen_stop(struct cmb_traffic_gen *tg);

/**
 * @brief Get number of packets generated.
 *
 * @param tg Pointer to a generator.
 * @return Number of packets generated.
 */
static inline uint64_t cmb_traffic_gen_packets_generated(const struct cmb_traffic_gen *tg)
{
    return tg->packets_generated;
}

static inline uint64_t cmb_traffic_gen_packets_sent(const struct cmb_traffic_gen *tg)
{
    return tg->packets_generated;
}

/**
 * @brief Check if generator is running.
 *
 * @param tg Pointer to a generator.
 * @return True if running.
 */
static inline bool cmb_traffic_gen_is_running(const struct cmb_traffic_gen *tg)
{
    return tg->running;
}

#endif /* CIMBA_CMB_TRAFFIC_GEN_H */