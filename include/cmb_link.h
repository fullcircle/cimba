/**
 * @file cmb_link.h
 * @brief Point-to-point link connecting two nodeswitches.
 *
 * A link models bandwidth (transmission capacity) and propagation delay.
 * Packets entering the link wait for available bandwidth, then traverse
 * the link over the propagation delay time.
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
#ifndef CIMBA_CMB_LINK_H
#define CIMBA_CMB_LINK_H

#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_buffer.h"

#define CMB_LINK_UNLIMITED_BANDWIDTH UINT64_MAX

/**
 * @brief Configuration for link creation.
 */
struct cmb_link_config {
    uint64_t bandwidth_bits_per_sec;
    double propagation_delay_sec;
    uint64_t buffer_capacity_bits;
};

/**
 * @brief A point-to-point link connecting two nodeswitches.
 *
 * The link uses a cmb_buffer to model the link bandwidth as bits that
 * can be "in flight" at any time. When a packet transmits, it puts its
 * bits onto the link buffer, waits for the transmission to complete
 * (based on bandwidth), then holds for propagation delay before
 * delivering to the destination.
 *
 * The source and destination nodeswitches and ports are stored for
 * reference but the link itself does not initiate delivery - the
 * calling packet process handles that.
 */
struct cmb_link {
    struct cmi_resourcebase core;

    struct cmb_buffer tx_buffer;

    uint64_t bandwidth_bits_per_sec;
    double propagation_delay_sec;

    void *src_node;
    void *dst_node;

    uint16_t src_port;
    uint16_t dst_port;

    uint64_t packets_transmitted;
    uint64_t bits_transmitted;
    uint64_t packets_dropped;
};

/**
 * @brief Allocate memory for a link.
 *
 * @memberof cmb_link
 * @return Pointer to the newly allocated link.
 */
extern struct cmb_link *cmb_link_create(void);

/**
 * @brief Initialize a link for use.
 *
 * @memberof cmb_link
 * @param lp Pointer to an already allocated link.
 * @param name Identifying name string.
 * @param src_node Source nodeswitch (user-provided, stored as void*).
 * @param src_port Port number on source switch.
 * @param dst_node Destination nodeswitch (user-provided, stored as void*).
 * @param dst_port Port number on destination switch.
 * @param config Link configuration (bandwidth, delay, buffer size).
 */
extern void cmb_link_initialize(struct cmb_link *lp,
                                const char *name,
                                void *src_node,
                                uint16_t src_port,
                                void *dst_node,
                                uint16_t dst_port,
                                const struct cmb_link_config *config);

/**
 * @brief Uninitialize a link.
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 */
extern void cmb_link_terminate(struct cmb_link *lp);

/**
 * @brief Free memory for a link.
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 */
extern void cmb_link_destroy(struct cmb_link *lp);

/**
 * @brief Transmit a packet across the link.
 *
 * This is a blocking call from the packet's process perspective.
 * The packet:
 * 1. Puts its bits onto the link's transmit buffer (waits if full)
 * 2. Waits for transmission to complete (based on packet size / bandwidth)
 * 3. Holds for propagation delay
 * 4. Returns (caller should then deliver to destination switch)
 *
 * @memberof cmb_link
 * @param lp Pointer to the link.
 * @param packet_size_bits Size of packet in bits.
 * @return CMB_PROCESS_SUCCESS if transmitted, error code otherwise.
 */
extern int64_t cmb_link_transmit(struct cmb_link *lp, uint32_t packet_size_bits);

/**
 * @brief Get the number of packets transmitted on this link.
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Number of packets transmitted.
 */
static inline uint64_t cmb_link_packets_transmitted(const struct cmb_link *lp)
{
    return lp->packets_transmitted;
}

/**
 * @brief Get the number of bits transmitted on this link.
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Number of bits transmitted.
 */
static inline uint64_t cmb_link_bits_transmitted(const struct cmb_link *lp)
{
    return lp->bits_transmitted;
}

/**
 * @brief Get the number of packets dropped on this link.
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Number of packets dropped.
 */
static inline uint64_t cmb_link_packets_dropped(const struct cmb_link *lp)
{
    return lp->packets_dropped;
}

/**
 * @brief Get the current utilization of the link (bits in buffer).
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Current bits in transit.
 */
static inline uint64_t cmb_link_utilization(struct cmb_link *lp)
{
    return cmb_buffer_level((struct cmb_buffer *)&lp->tx_buffer);
}

/**
 * @brief Get source nodeswitch (as void*).
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Source nodeswitch pointer.
 */
static inline void *cmb_link_src_node(const struct cmb_link *lp)
{
    return lp->src_node;
}

/**
 * @brief Get destination nodeswitch (as void*).
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Destination nodeswitch pointer.
 */
static inline void *cmb_link_dst_node(const struct cmb_link *lp)
{
    return lp->dst_node;
}

/**
 * @brief Get destination port.
 *
 * @memberof cmb_link
 * @param lp Pointer to a link.
 * @return Destination port number.
 */
static inline uint16_t cmb_link_dst_port(const struct cmb_link *lp)
{
    return lp->dst_port;
}

#endif /* CIMBA_CMB_LINK_H */