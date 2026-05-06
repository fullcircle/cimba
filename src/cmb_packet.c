/**
 * @file cmb_packet.c
 * @brief Packet implementation - active process that moves through network.
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
#include <string.h>

#include "cmb_packet.h"
#include "cmb_event.h"

static enum cmb_packet_stats_level global_stats_level = CMB_PACKET_STATS_AGGREGATE;

static struct cmb_packet *packet_pool[CMB_PACKET_POOL_CAPACITY];
static uint32_t packet_pool_count = 0;
static bool packet_pool_initialized = false;

void cmb_packet_pool_initialize(void)
{
    for (uint32_t i = 0; i < CMB_PACKET_POOL_CAPACITY; i++) {
        packet_pool[i] = NULL;
    }
    packet_pool_count = 0;
    packet_pool_initialized = true;
}

void cmb_packet_pool_terminate(void)
{
    packet_pool_initialized = false;
}

struct cmb_packet *cmb_packet_create(void)
{
    struct cmb_packet *pp;

    if (packet_pool_initialized && packet_pool_count < CMB_PACKET_POOL_CAPACITY) {
        pp = (struct cmb_packet *)cmb_process_create();
        if (pp != NULL) {
            packet_pool[packet_pool_count++] = pp;
        }
        return pp;
    }

    return (struct cmb_packet *)cmb_process_create();
}

void cmb_packet_destroy(struct cmb_packet *pp)
{
    cmb_process_destroy(&pp->proc);
}

void cmb_packet_initialize(struct cmb_packet *pp,
                            const uint32_t src,
                            const uint32_t dst,
                            const uint32_t size_bits,
                            const int64_t priority)
{
    pp->src_addr = src;
    pp->dst_addr = dst;
    pp->size_bits = size_bits;
    pp->ttl = 64;
    pp->vlan_id = 0;
    pp->vlan_pcp = 0;
    pp->qos = CMB_QOS_BEST_EFFORT;
    pp->ecn = CMB_ECN_ECT_0;
    pp->port_in = 0;
    pp->port_out = 0;
    pp->payload = NULL;
    pp->payload_size = 0;
    pp->owner = NULL;

    memset(&pp->stats, 0, sizeof(pp->stats));

    pp->creation_time = cmb_time();
    pp->stats.queue_enter_time = -1;
    pp->stats.queue_exit_time = -1;
    pp->stats.tx_start_time = -1;
    pp->stats.rx_time = -1;
    pp->stats.drop_time = -1;
    pp->stats.hops = 0;
    pp->stats.ecn_ce_marked = false;
}

void cmb_packet_stats_level_set(enum cmb_packet_stats_level level)
{
    global_stats_level = level;
}

enum cmb_packet_stats_level cmb_packet_stats_level_get(void)
{
    return global_stats_level;
}

void cmb_packet_stat_record(struct cmb_packet *pp,
                            enum cmb_packet_stat_type stat,
                            int64_t value)
{
    if (global_stats_level == CMB_PACKET_STATS_NONE) {
        return;
    }

    switch (stat) {
    case CMB_PACKET_STAT_CREATION_TIME:
        pp->stats.queue_enter_time = value;
        break;
    case CMB_PACKET_STAT_QUEUE_ENTER_TIME:
        pp->stats.queue_enter_time = value;
        break;
    case CMB_PACKET_STAT_QUEUE_EXIT_TIME:
        pp->stats.queue_exit_time = value;
        break;
    case CMB_PACKET_STAT_TRANSMISSION_START_TIME:
        pp->stats.tx_start_time = value;
        break;
    case CMB_PACKET_STAT_RECEPTION_TIME:
        pp->stats.rx_time = value;
        break;
    case CMB_PACKET_STAT_DROP_TIME:
        pp->stats.drop_time = value;
        break;
    case CMB_PACKET_STAT_END_TO_END_DELAY:
        pp->stats.end_to_end_delay = value;
        break;
    case CMB_PACKET_STAT_QUEUE_DELAY:
        pp->stats.queue_delay = value;
        break;
    case CMB_PACKET_STAT_TRANSMISSION_TIME:
        pp->stats.tx_time = value;
        break;
    case CMB_PACKET_STAT_HOPS:
        pp->stats.hops = (int32_t)value;
        break;
    case CMB_PACKET_STAT_SIZE:
        break;
    }
}

int64_t cmb_packet_stat_get(const struct cmb_packet *pp,
                          enum cmb_packet_stat_type stat)
{
    switch (stat) {
    case CMB_PACKET_STAT_CREATION_TIME:
    case CMB_PACKET_STAT_QUEUE_ENTER_TIME:
        return pp->stats.queue_enter_time;
    case CMB_PACKET_STAT_QUEUE_EXIT_TIME:
        return pp->stats.queue_exit_time;
    case CMB_PACKET_STAT_TRANSMISSION_START_TIME:
        return pp->stats.tx_start_time;
    case CMB_PACKET_STAT_RECEPTION_TIME:
        return pp->stats.rx_time;
    case CMB_PACKET_STAT_DROP_TIME:
        return pp->stats.drop_time;
    case CMB_PACKET_STAT_END_TO_END_DELAY:
        return pp->stats.end_to_end_delay;
    case CMB_PACKET_STAT_QUEUE_DELAY:
        return pp->stats.queue_delay;
    case CMB_PACKET_STAT_TRANSMISSION_TIME:
        return pp->stats.tx_time;
    case CMB_PACKET_STAT_HOPS:
        return pp->stats.hops;
    case CMB_PACKET_STAT_SIZE:
        return pp->size_bits;
    }
    return 0;
}