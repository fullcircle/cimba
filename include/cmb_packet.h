/**
 * @file cmb_packet.h
 * @brief Packet as an active process that moves through the network.
 *
 * A packet is a cmb_process that traverses the network from source to
 * destination, passing through nodeswitches and links. This design facilitates
 * debugging as packet movement can be traced through the simulation like a
 * regular process.
 *
 * VLAN and QoS markings are supported for traffic differentiation.
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
#ifndef CIMBA_CMB_PACKET_H
#define CIMBA_CMB_PACKET_H

#include <stdint.h>
#include <stdbool.h>

#include "cmb_process.h"

#define CMB_PACKET_IPV4_ADDR_SZ 4

#define CMB_PACKET_POOL_CAPACITY 4096

/**
 * @brief QoS priority levels for packet scheduling.
 */
enum cmb_qos_level {
    CMB_QOS_BEST_EFFORT = 0,
    CMB_QOS_PRIORITY = 1,
    CMB_QOS_VIDEO = 2,
    CMB_QOS_VOICE = 3,
    CMB_QOS_CONTROL = 4
};

/**
 * @brief ECN (Explicit Congestion Notification) bits.
 *
 * RFC 3168 defines:
 * - ECT(0): ECN-capable transport, codepoint 10
 * - ECT(1): ECN-capable transport, codepoint 01
 * - CE: Congestion Experienced
 */
enum cmb_ecn_bits {
    CMB_ECN_ECT_0 = 0x02,
    CMB_ECN_ECT_1 = 0x01,
    CMB_ECN_CE    = 0x03,
    CMB_ECN_NOT_ECT = 0x00
};

/**
 * @brief Statistics collected for packets (detailed and aggregate levels).
 */
enum cmb_packet_stat_type {
    CMB_PACKET_STAT_CREATION_TIME,
    CMB_PACKET_STAT_QUEUE_ENTER_TIME,
    CMB_PACKET_STAT_QUEUE_EXIT_TIME,
    CMB_PACKET_STAT_TRANSMISSION_START_TIME,
    CMB_PACKET_STAT_RECEPTION_TIME,
    CMB_PACKET_STAT_DROP_TIME,
    CMB_PACKET_STAT_END_TO_END_DELAY,
    CMB_PACKET_STAT_QUEUE_DELAY,
    CMB_PACKET_STAT_TRANSMISSION_TIME,
    CMB_PACKET_STAT_HOPS,
    CMB_PACKET_STAT_SIZE
};

/**
 * @brief Statistics collection level.
 */
enum cmb_packet_stats_level {
    CMB_PACKET_STATS_NONE = 0,
    CMB_PACKET_STATS_AGGREGATE = 1,
    CMB_PACKET_STATS_DETAILED = 2
};

/**
 * @brief The packet structure, extending cmb_process to allow active
 *        movement through the network.
 *
 * The packet process function should implement the routing logic: wait
 * at the current node for processing, then move to the next node via
 * a link.
 */
struct cmb_packet {
    struct cmb_process proc;

    uint32_t src_addr;
    uint32_t dst_addr;

    uint32_t size_bits;

    uint16_t vlan_id;
    uint16_t vlan_pcp;

    enum cmb_qos_level qos;
    enum cmb_ecn_bits ecn;

    uint8_t ttl;

    uint16_t port_in;
    uint16_t port_out;

    int64_t creation_time;

    struct {
        int64_t queue_enter_time;
        int64_t queue_exit_time;
        int64_t tx_start_time;
        int64_t rx_time;
        int64_t drop_time;
        int64_t end_to_end_delay;
        int64_t queue_delay;
        int64_t tx_time;
        int32_t hops;
        bool ecn_ce_marked;
    } stats;

    void *payload;
    uint32_t payload_size;

    void *owner;
};

/**
 * @brief Allocate memory for a packet.
 *
 * @memberof cmb_packet
 * @return Pointer to the newly allocated packet.
 */
extern struct cmb_packet *cmb_packet_create(void);

/**
 * @brief Initialize a packet for use.
 *
 * @memberof cmb_packet
 * @param pp Pointer to an already allocated packet.
 * @param src Source IPv4 address (4 bytes).
 * @param dst Destination IPv4 address (4 bytes).
 * @param size_bits Packet size in bits.
 * @param priority Process priority for queueing.
 */
extern void cmb_packet_initialize(struct cmb_packet *pp,
                                   const uint32_t src,
                                   const uint32_t dst,
                                   const uint32_t size_bits,
                                   const int64_t priority);

/**
 * @brief Uninitialize a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 */
extern void cmb_packet_terminate(struct cmb_packet *pp);

/**
 * @brief Free memory for a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 */
extern void cmb_packet_destroy(struct cmb_packet *pp);

/**
 * @brief Set VLAN tag on a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @param vlan_id The VLAN identifier (0-4094).
 * @param pcp Priority Code Point (0-7).
 */
static inline void cmb_packet_set_vlan(struct cmb_packet *pp,
                                       const uint16_t vlan_id,
                                       const uint8_t pcp)
{
    pp->vlan_id = vlan_id;
    pp->vlan_pcp = pcp;
}

/**
 * @brief Set QoS level on a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @param qos The QoS level.
 */
static inline void cmb_packet_set_qos(struct cmb_packet *pp, const enum cmb_qos_level qos)
{
    pp->qos = qos;
}

/**
 * @brief Decrement TTL and return true if packet should be dropped.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @return True if TTL expired, false otherwise.
 */
static inline bool cmb_packet_ttl_expired(struct cmb_packet *pp)
{
    return (pp->ttl-- == 0);
}

/**
 * @brief Get packet priority for queueing based on QoS and VLAN PCP.
 *
 * Higher values are dequeued first.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @return Combined priority value.
 */
static inline int64_t cmb_packet_queue_priority(const struct cmb_packet *pp)
{
    return ((int64_t)pp->qos * 8) + pp->vlan_pcp;
}

/**
 * @brief Set ECN bits on a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @param ecn The ECN bits value.
 */
static inline void cmb_packet_set_ecn(struct cmb_packet *pp, enum cmb_ecn_bits ecn)
{
    pp->ecn = ecn;
}

/**
 * @brief Get ECN bits from a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @return The ECN bits value.
 */
static inline enum cmb_ecn_bits cmb_packet_get_ecn(const struct cmb_packet *pp)
{
    return pp->ecn;
}

/**
 * @brief Check if packet has CE (Congestion Experienced) mark.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @return True if CE bit is set.
 */
static inline bool cmb_packet_is_ce_marked(const struct cmb_packet *pp)
{
    return pp->ecn == CMB_ECN_CE;
}

/**
 * @brief Mark packet with CE (Congestion Experienced).
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 */
static inline void cmb_packet_mark_ce(struct cmb_packet *pp)
{
    pp->ecn = CMB_ECN_CE;
    pp->stats.ecn_ce_marked = true;
}

/**
 * @brief Check if packet was CE marked in transit.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @return True if CE marked.
 */
static inline bool cmb_packet_was_ce_marked(const struct cmb_packet *pp)
{
    return pp->stats.ecn_ce_marked;
}

/**
 * @brief Set packet stats collection level.
 *
 * @memberof cmb_packet
 * @param level The stats collection level.
 */
extern void cmb_packet_stats_level_set(enum cmb_packet_stats_level level);

/**
 * @brief Get current stats collection level.
 *
 * @return The current stats collection level.
 */
extern enum cmb_packet_stats_level cmb_packet_stats_level_get(void);

/**
 * @brief Initialize the packet memory pool.
 *
 * Call this before creating any packets if using the pool.
 *
 * @memberof cmb_packet
 */
extern void cmb_packet_pool_initialize(void);

/**
 * @brief Terminate the packet memory pool.
 *
 * @memberof cmb_packet
 */
extern void cmb_packet_pool_terminate(void);

/**
 * @brief Record a stat for a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @param stat The stat type.
 * @param value The stat value.
 */
extern void cmb_packet_stat_record(struct cmb_packet *pp,
                                   enum cmb_packet_stat_type stat,
                                   int64_t value);

/**
 * @brief Get a stat value from a packet.
 *
 * @memberof cmb_packet
 * @param pp Pointer to a packet.
 * @param stat The stat type.
 * @return The stat value.
 */
extern int64_t cmb_packet_stat_get(const struct cmb_packet *pp,
                                  enum cmb_packet_stat_type stat);

#endif /* CIMBA_CMB_PACKET_H */