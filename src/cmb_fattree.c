/**
 * @file cmb_fattree.c
 * @brief Fat-tree (k-ary-3) network topology generator implementation.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmb_fattree.h"
#include "cmb_packet.h"
#include "cmb_event.h"
#include "cmb_random.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"

static struct cmb_nodeswitch_port_config make_port_config(const struct cmb_fattree_config *cfg)
{
    struct cmb_nodeswitch_port_config port_cfg = {
        .queue_depth = cfg->queue_depth,
        .queue_type = cfg->queue_type,
        .port_speed_bits_per_sec = cfg->link_bandwidth_bps,
        .drop_on_overflow = true,
        .enable_ecn = cfg->enable_ecn,
        .ecn_kmin = cfg->ecn_kmin,
        .ecn_kmax = cfg->ecn_kmax,
        .enable_red = cfg->enable_red,
        .red_min_th = cfg->red_min_th,
        .red_max_th = cfg->red_max_th,
        .red_max_prob = cfg->red_max_prob,
        .red_q_w = cfg->red_q_w
    };
    return port_cfg;
}

static struct cmb_link_config make_link_config(const struct cmb_fattree_config *cfg)
{
    struct cmb_link_config link_cfg = {
        .bandwidth_bits_per_sec = cfg->link_bandwidth_bps,
        .propagation_delay_sec = cfg->link_propagation_delay_sec,
        .buffer_capacity_bits = cfg->link_bandwidth_bps
    };
    return link_cfg;
}

static uint32_t calc_total_links(uint16_t k)
{
    uint16_t half_k = k / 2;

    uint32_t host_to_edge = k * half_k;
    uint32_t edge_to_agg = k * half_k * half_k;
    uint32_t agg_to_core = k * half_k * half_k;

    return host_to_edge + edge_to_agg + agg_to_core;
}

struct cmb_fattree *cmb_fattree_create(void)
{
    struct cmb_fattree *ft = cmi_malloc(sizeof(*ft));
    cmi_memset(ft, 0, sizeof(*ft));
    return ft;
}

void cmb_fattree_initialize(struct cmb_fattree *ft, const struct cmb_fattree_config *cfg)
{
    cmb_assert_release(ft != NULL);
    cmb_assert_release(cfg != NULL);
    cmb_assert_release(cfg->k >= 4);
    cmb_assert_release(cfg->k % 2 == 0);

    ft->k = cfg->k;
    ft->num_pods = cfg->k;
    ft->num_edge_per_pod = cfg->k / 2;
    ft->num_agg_per_pod = cfg->k / 2;
    ft->num_core = (cfg->k / 2) * (cfg->k / 2);

    uint16_t total_hosts = ft->num_pods * ft->num_edge_per_pod;
    ft->num_links = calc_total_links(cfg->k);

    ft->net = cmb_network_create();
    cmb_network_initialize(ft->net, &(struct cmb_network_config){
        .stats_level = CMB_NETWORK_STATS_AGGREGATE,
        .rng_seed = 0
    });

    struct cmb_nodeswitch_port_config port_cfg = make_port_config(cfg);
    struct cmb_link_config link_cfg = make_link_config(cfg);

    ft->edge_switches = cmi_malloc(ft->num_pods * ft->num_edge_per_pod * sizeof(struct cmb_nodeswitch *));
    ft->agg_switches = cmi_malloc(ft->num_pods * ft->num_agg_per_pod * sizeof(struct cmb_nodeswitch *));
    ft->core_switches = cmi_malloc(ft->num_core * sizeof(struct cmb_nodeswitch *));
    ft->hosts = cmi_malloc(total_hosts * sizeof(struct cmb_traffic_sink *));
    ft->links = cmi_malloc(ft->num_links * sizeof(struct cmb_link *));

    uint32_t link_idx = 0;

    for (uint16_t pod = 0; pod < ft->num_pods; pod++) {
        for (uint16_t e = 0; e < ft->num_edge_per_pod; e++) {
            char name[32];
            snprintf(name, sizeof(name), "pod%u-edge%u", pod, e);
            struct cmb_nodeswitch *sw = cmb_nodeswitch_create();
            cmb_nodeswitch_initialize(sw, name, cfg->k, &port_cfg);

            uint16_t edge_idx = pod * ft->num_edge_per_pod + e;
            uint32_t host_addr = (uint32_t)((pod << 16) | (e << 8) | 1);
            cmb_nodeswitch_set_local_addr(sw, host_addr);
            cmb_network_add_switch(ft->net, sw);
            ft->edge_switches[edge_idx] = sw;
        }
    }

    for (uint16_t pod = 0; pod < ft->num_pods; pod++) {
        for (uint16_t a = 0; a < ft->num_agg_per_pod; a++) {
            char name[32];
            snprintf(name, sizeof(name), "pod%u-agg%u", pod, a);
            struct cmb_nodeswitch *sw = cmb_nodeswitch_create();
            cmb_nodeswitch_initialize(sw, name, cfg->k, &port_cfg);
            cmb_network_add_switch(ft->net, sw);

            uint16_t agg_idx = pod * ft->num_agg_per_pod + a;
            ft->agg_switches[agg_idx] = sw;
        }
    }

    for (uint16_t c = 0; c < ft->num_core; c++) {
        char name[32];
        snprintf(name, sizeof(name), "core%u", c);
        struct cmb_nodeswitch *sw = cmb_nodeswitch_create();
        cmb_nodeswitch_initialize(sw, name, cfg->k, &port_cfg);
        cmb_network_add_switch(ft->net, sw);
        ft->core_switches[c] = sw;
    }

    for (uint16_t pod = 0; pod < ft->num_pods; pod++) {
        for (uint16_t e = 0; e < ft->num_edge_per_pod; e++) {
            uint16_t edge_idx = pod * ft->num_edge_per_pod + e;
            struct cmb_nodeswitch *edge_sw = ft->edge_switches[edge_idx];

            uint16_t host_id = pod * ft->num_edge_per_pod + e;

            char sink_name[32];
            snprintf(sink_name, sizeof(sink_name), "host%u", host_id);
            struct cmb_traffic_sink *sink = cmb_traffic_sink_create();
            uint32_t host_addr = (uint32_t)((pod << 16) | (e << 8) | 1);
            cmb_traffic_sink_initialize(sink, sink_name, edge_sw, host_addr);
            ft->hosts[host_id] = sink;

            struct cmb_link *link = cmb_link_create();
            char link_name[32];
            snprintf(link_name, sizeof(link_name), "h%u-s%u", host_id, edge_idx);
            cmb_link_initialize(link, link_name, edge_sw, e, sink, 0, &link_cfg);
            ft->links[link_idx++] = link;

            cmb_network_add_traffic_sink(ft->net, sink);
        }
    }

    uint16_t base_agg_port = ft->num_edge_per_pod;

    for (uint16_t pod = 0; pod < ft->num_pods; pod++) {
        for (uint16_t e = 0; e < ft->num_edge_per_pod; e++) {
            uint16_t edge_idx = pod * ft->num_edge_per_pod + e;
            struct cmb_nodeswitch *edge_sw = ft->edge_switches[edge_idx];

            for (uint16_t a = 0; a < ft->num_agg_per_pod; a++) {
                uint16_t agg_idx = pod * ft->num_agg_per_pod + a;
                struct cmb_nodeswitch *agg_sw = ft->agg_switches[agg_idx];

                uint16_t edge_port = base_agg_port + a;
                uint16_t agg_port = e * ft->num_agg_per_pod + a;

                if (edge_port >= ft->k || agg_port >= ft->k) {
                    fprintf(stderr, "ERROR: edge %u agg %u: edge_port=%u agg_port=%u k=%u\n",
                            e, a, edge_port, agg_port, ft->k);
                }

                struct cmb_link *link = cmb_link_create();
                char link_name[32];
                snprintf(link_name, sizeof(link_name), "e%u-a%u", edge_idx, agg_idx);
                cmb_link_initialize(link, link_name, edge_sw, edge_port, agg_sw, agg_port, &link_cfg);
                ft->links[link_idx++] = link;

                cmb_nodeswitch_set_port_link(edge_sw, edge_port, link, 0);
                cmb_nodeswitch_route_add(edge_sw, 0xFFFFFFFF, edge_port, link);
            }
        }
    }

    for (uint16_t pod = 0; pod < ft->num_pods; pod++) {
        for (uint16_t a = 0; a < ft->num_agg_per_pod; a++) {
            uint16_t agg_idx = pod * ft->num_agg_per_pod + a;
            struct cmb_nodeswitch *agg_sw = ft->agg_switches[agg_idx];

            uint16_t base_core_port = ft->num_edge_per_pod;

            for (uint16_t i = 0; i < ft->num_agg_per_pod; i++) {
                uint16_t c = (a + i * ft->num_agg_per_pod) % ft->num_core;
                struct cmb_nodeswitch *core_sw = ft->core_switches[c];

                uint16_t agg_port = base_core_port + i;
                uint16_t core_port_in_core = pod;

                if (agg_port >= ft->k) {
                    fprintf(stderr, "ERROR: agg_port %u >= k %u for agg %u in pod %u\n", agg_port, ft->k, a, pod);
                }
                if (core_port_in_core >= ft->k) {
                    fprintf(stderr, "ERROR: core_port_in_core %u >= k %u for core %u, pod %u\n", core_port_in_core, ft->k, c, pod);
                }

                struct cmb_link *link = cmb_link_create();
                char link_name[32];
                snprintf(link_name, sizeof(link_name), "a%u-c%u", agg_idx, c);
                cmb_link_initialize(link, link_name, agg_sw, agg_port, core_sw, core_port_in_core, &link_cfg);
                ft->links[link_idx++] = link;

                cmb_nodeswitch_set_port_link(agg_sw, agg_port, link, 0);
                cmb_nodeswitch_route_add(agg_sw, 0xFFFFFFFF, agg_port, link);

                cmb_nodeswitch_set_port_link(core_sw, core_port_in_core, link, 0);
                cmb_nodeswitch_route_add(core_sw, (uint32_t)((core_port_in_core << 16) | 0x00FFFFFF), core_port_in_core, link);
            }
        }
    }

    for (uint16_t link_i = 0; link_i < ft->num_links; link_i++) {
        cmb_network_add_link(ft->net, ft->links[link_i]);
    }

    cmb_network_start(ft->net);
}

void cmb_fattree_terminate(struct cmb_fattree *ft)
{
    if (ft == NULL) return;

    if (ft->net != NULL) {
        cmb_network_destroy(ft->net);
        ft->net = NULL;
    }

    cmi_free(ft->links);
    ft->links = NULL;

    cmi_free(ft->hosts);
    ft->hosts = NULL;

    cmi_free(ft->core_switches);
    ft->core_switches = NULL;

    cmi_free(ft->agg_switches);
    ft->agg_switches = NULL;

    cmi_free(ft->edge_switches);
    ft->edge_switches = NULL;
}

void cmb_fattree_destroy(struct cmb_fattree *ft)
{
    if (ft == NULL) return;

    cmb_fattree_terminate(ft);
    cmi_free(ft);
}

uint16_t cmb_fattree_num_hosts(const struct cmb_fattree *ft)
{
    if (ft == NULL) return 0;
    return ft->num_pods * ft->num_edge_per_pod;
}

uint16_t cmb_fattree_num_switches(const struct cmb_fattree *ft)
{
    if (ft == NULL) return 0;
    return ft->num_pods * (ft->num_edge_per_pod + ft->num_agg_per_pod) + ft->num_core;
}

uint32_t cmb_fattree_num_links(const struct cmb_fattree *ft)
{
    if (ft == NULL) return 0;
    return ft->num_links;
}

struct cmb_traffic_sink *cmb_fattree_get_host(const struct cmb_fattree *ft, uint16_t host_id)
{
    if (ft == NULL || host_id >= cmb_fattree_num_hosts(ft)) return NULL;
    return ft->hosts[host_id];
}

uint32_t cmb_fattree_host_addr(const struct cmb_fattree *ft, uint16_t host_id)
{
    if (ft == NULL || host_id >= cmb_fattree_num_hosts(ft)) return 0;
    uint16_t pod = host_id / ft->num_edge_per_pod;
    uint16_t e = host_id % ft->num_edge_per_pod;
    return (uint32_t)((pod << 16) | (e << 8) | 1);
}

struct cmb_nodeswitch *cmb_fattree_host_switch(const struct cmb_fattree *ft, uint16_t host_id)
{
    if (ft == NULL || host_id >= cmb_fattree_num_hosts(ft)) return NULL;
    uint16_t pod = host_id / ft->num_edge_per_pod;
    uint16_t e = host_id % ft->num_edge_per_pod;
    uint16_t edge_idx = pod * ft->num_edge_per_pod + e;
    return ft->edge_switches[edge_idx];
}

void cmb_fattree_print_stats(const struct cmb_fattree *ft, FILE *fp)
{
    if (ft == NULL || fp == NULL) return;

    fprintf(fp, "\n=== FatTree(k=%u) Statistics ===\n", ft->k);
    fprintf(fp, "Pods: %u\n", ft->num_pods);
    fprintf(fp, "Edge switches: %u\n", ft->num_pods * ft->num_edge_per_pod);
    fprintf(fp, "Aggregation switches: %u\n", ft->num_pods * ft->num_agg_per_pod);
    fprintf(fp, "Core switches: %u\n", ft->num_core);
    fprintf(fp, "Total switches: %u\n", cmb_fattree_num_switches(ft));
    fprintf(fp, "Total hosts: %u\n", cmb_fattree_num_hosts(ft));
    fprintf(fp, "Total links: %u\n", ft->num_links);

    if (ft->net != NULL) {
        cmb_network_print_stats(ft->net, fp);
    }
}
