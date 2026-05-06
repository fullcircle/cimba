/**
 * @file cmb_fattree.h
 * @brief Fat-tree (k-ary-3) network topology generator.
 *
 * A fat-tree (k-ary-3) has three tiers:
 * - Edge switches: connect hosts to the network
 * - Aggregation switches: connect edge to core
 * - Core switches: connect pods together
 *
 * Topology for k ports per switch:
 * - k pods
 * - k/2 edge switches per pod
 * - k/2 aggregation switches per pod
 * - (k/2)^2 core switches
 * - Total hosts = k^2 / 2
 *
 * Example: k=16 yields 1024 hosts, 320 switches
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
#ifndef CIMBA_CMB_FATTREE_H
#define CIMBA_CMB_FATTREE_H

#include <stdint.h>
#include <stdbool.h>

#include "cmb_network.h"
#include "cmb_nodeswitch.h"
#include "cmb_link.h"
#include "cmb_traffic_sink.h"

struct cmb_fattree_config {
    uint16_t k;
    uint64_t link_bandwidth_bps;
    double link_propagation_delay_sec;
    uint64_t queue_depth;
    enum cmb_queue_type queue_type;
    bool enable_ecn;
    uint64_t ecn_kmin;
    uint64_t ecn_kmax;
    bool enable_red;
    uint64_t red_min_th;
    uint64_t red_max_th;
    double red_max_prob;
    double red_q_w;
};

struct cmb_fattree {
    struct cmb_network *net;
    uint16_t k;
    uint16_t num_pods;
    uint16_t num_edge_per_pod;
    uint16_t num_agg_per_pod;
    uint16_t num_core;

    struct cmb_nodeswitch **edge_switches;
    struct cmb_nodeswitch **agg_switches;
    struct cmb_nodeswitch **core_switches;
    struct cmb_traffic_sink **hosts;

    struct cmb_link **links;
    uint32_t num_links;
};

extern struct cmb_fattree *cmb_fattree_create(void);

extern void cmb_fattree_initialize(struct cmb_fattree *ft,
                                   const struct cmb_fattree_config *config);

extern void cmb_fattree_terminate(struct cmb_fattree *ft);

extern void cmb_fattree_destroy(struct cmb_fattree *ft);

extern uint16_t cmb_fattree_num_hosts(const struct cmb_fattree *ft);

extern uint16_t cmb_fattree_num_switches(const struct cmb_fattree *ft);

extern uint32_t cmb_fattree_num_links(const struct cmb_fattree *ft);

extern struct cmb_traffic_sink *cmb_fattree_get_host(const struct cmb_fattree *ft,
                                                     uint16_t host_id);

extern uint32_t cmb_fattree_host_addr(const struct cmb_fattree *ft, uint16_t host_id);

extern uint16_t cmb_fattree_host_switch_port(const struct cmb_fattree *ft,
                                              uint16_t host_id);

extern struct cmb_nodeswitch *cmb_fattree_host_switch(const struct cmb_fattree *ft,
                                                      uint16_t host_id);

extern void cmb_fattree_print_stats(const struct cmb_fattree *ft, FILE *fp);

#endif /* CIMBA_CMB_FATTREE_H */
