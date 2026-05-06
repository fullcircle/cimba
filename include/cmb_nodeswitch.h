/**
 * @file cmb_nodeswitch.h
 * @brief Network switch node with configurable ports, queues, and routing.
 *
 * A nodeswitch is a switching element with multiple ports. Each port can
 * have its own queue type (FIFO or priority) and depth. The switch maintains
 * a static routing table for determining output ports based on destination
 * address.
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
#ifndef CIMBA_CMB_NODESWITCH_H
#define CIMBA_CMB_NODESWITCH_H

#include <stdint.h>

#include "cmb_assert.h"
#include "cmb_objectqueue.h"
#include "cmb_priorityqueue.h"
#include "cmb_link.h"

#define CMB_NODESWITCH_MAX_PORTS 64

/**
 * @brief Queue type for a port.
 */
enum cmb_queue_type {
    CMB_QUEUE_FIFO = 0,
    CMB_QUEUE_PRIORITY = 1
};

/**
 * @brief Port configuration.
 */
struct cmb_nodeswitch_port_config {
    uint64_t queue_depth;
    enum cmb_queue_type queue_type;
    uint64_t port_speed_bits_per_sec;
    bool drop_on_overflow;

    bool enable_ecn;
    uint64_t ecn_kmin;
    uint64_t ecn_kmax;
};

/**
 * @brief A port on a nodeswitch.
 */
struct cmb_nodeswitch_port {
    struct cmb_objectqueue *fifo_queue;
    struct cmb_priorityqueue *priority_queue;
    uint64_t queue_depth;
    enum cmb_queue_type queue_type;
    uint64_t port_speed_bits_per_sec;
    bool drop_on_overflow;

    bool enable_ecn;
    uint64_t ecn_kmin;
    uint64_t ecn_kmax;

    struct cmb_link *link_out;
    uint16_t link_out_port;

    uint64_t packets_received;
    uint64_t packets_sent;
    uint64_t packets_dropped_overflow;
    uint64_t packets_ecn_marked;

    uint64_t current_queue_depth;
    uint64_t min_queue_depth;
    uint64_t max_queue_depth;
    uint64_t total_queue_depth;
    uint64_t queue_samples;
};

/**
 * @brief Routing table entry.
 */
struct cmb_nodeswitch_route {
    uint32_t dest_addr;
    uint16_t out_port;
    struct cmb_link *link;
};

/**
 * @brief A network switch node with configurable ports and queues.
 */
struct cmb_nodeswitch {
    struct cmi_resourcebase core;

    uint16_t port_count;
    struct cmb_nodeswitch_port *ports;

    struct cmb_nodeswitch_route *routing_table;
    uint16_t routing_table_size;
    uint16_t routing_table_capacity;

    struct cmb_objectqueue *input_queue;
    struct cmb_objectqueue *local_delivery_queue;
    uint32_t local_addr;

    struct cmb_process *processor;
    bool is_running;

    uint64_t packets_processed;
    uint64_t packets_dropped_no_route;
    uint64_t packets_dropped_overflow;
};

/**
 * @brief Allocate memory for a nodeswitch.
 *
 * @memberof cmb_nodeswitch
 * @return Pointer to the newly allocated nodeswitch.
 */
extern struct cmb_nodeswitch *cmb_nodeswitch_create(void);

/**
 * @brief Initialize a nodeswitch for use.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to an already allocated nodeswitch.
 * @param name Identifying name string.
 * @param port_count Number of ports.
 * @param port_configs Array of port configurations (must be port_count long).
 */
extern void cmb_nodeswitch_initialize(struct cmb_nodeswitch *nsp,
                                      const char *name,
                                      uint16_t port_count,
                                      const struct cmb_nodeswitch_port_config *port_configs);

/**
 * @brief Uninitialize a nodeswitch.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 */
extern void cmb_nodeswitch_terminate(struct cmb_nodeswitch *nsp);

/**
 * @brief Free memory for a nodeswitch.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 */
extern void cmb_nodeswitch_destroy(struct cmb_nodeswitch *nsp);

/**
 * @brief Set the output link for a port.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param port Port number.
 * @param link The link to use for output.
 * @param link_port The port number on the link.
 */
extern void cmb_nodeswitch_set_port_link(struct cmb_nodeswitch *nsp,
                                          uint16_t port,
                                          struct cmb_link *link,
                                          uint16_t link_port);

/**
 * @brief Add a static route to the routing table.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param dest_addr Destination address prefix (IPv4).
 * @param out_port Output port number.
 * @param link The link to use for this route.
 */
extern void cmb_nodeswitch_route_add(struct cmb_nodeswitch *nsp,
                                      uint32_t dest_addr,
                                      uint16_t out_port,
                                      struct cmb_link *link);

/**
 * @brief Look up a route for a destination address.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param dest_addr Destination address.
 * @param out_port Output port number (output).
 * @param link Link for the route (output).
 * @return True if route found, false otherwise.
 */
extern bool cmb_nodeswitch_route_lookup(struct cmb_nodeswitch *nsp,
                                         uint32_t dest_addr,
                                         uint16_t *out_port,
                                         struct cmb_link **link);

/**
 * @brief Set the local address for this switch.
 *
 * Used for local delivery of packets addressed to this switch.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param addr Local address.
 */
extern void cmb_nodeswitch_set_local_addr(struct cmb_nodeswitch *nsp, uint32_t addr);

/**
 * @brief Get the local delivery queue.
 *
 * Packets addressed to this switch's local address are delivered here.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @return Pointer to the local delivery queue.
 */
extern struct cmb_objectqueue *cmb_nodeswitch_get_local_delivery_queue(struct cmb_nodeswitch *nsp);

/**
 * @brief Enqueue a packet for processing at this switch.
 *
 * Packets arrive at the switch and are queued for processing.
 * The packet's process will be resumed when it can be processed.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param packet Pointer to the packet to enqueue.
 */
extern void cmb_nodeswitch_enqueue(struct cmb_nodeswitch *nsp, void *packet);

/**
 * @brief Start the switch packet processor.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 */
extern void cmb_nodeswitch_start(struct cmb_nodeswitch *nsp);

/**
 * @brief Stop the switch packet processor.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 */
extern void cmb_nodeswitch_stop(struct cmb_nodeswitch *nsp);

/**
 * @brief Get the queue for a port.
 *
 * Returns either the FIFO or priority queue based on port configuration.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param port Port number.
 * @return Pointer to the port's queue (caller must cast to correct type).
 */
extern void *cmb_nodeswitch_get_port_queue(struct cmb_nodeswitch *nsp, uint16_t port);

/**
 * @brief Get packet count received on a port.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param port Port number.
 * @return Number of packets received.
 */
static inline uint64_t cmb_nodeswitch_port_packets_received(const struct cmb_nodeswitch *nsp,
                                                            uint16_t port)
{
    if (port >= nsp->port_count) return 0;
    return nsp->ports[port].packets_received;
}

/**
 * @brief Get packet count sent from a port.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param port Port number.
 * @return Number of packets sent.
 */
static inline uint64_t cmb_nodeswitch_port_packets_sent(const struct cmb_nodeswitch *nsp,
                                                         uint16_t port)
{
    if (port >= nsp->port_count) return 0;
    return nsp->ports[port].packets_sent;
}

/**
 * @brief Get packet count dropped at a port.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @param port Port number.
 * @return Number of packets dropped.
 */
static inline uint64_t cmb_nodeswitch_port_packets_dropped(const struct cmb_nodeswitch *nsp,
                                                            uint16_t port)
{
    if (port >= nsp->port_count) return 0;
    return nsp->ports[port].packets_dropped_overflow;
}

static inline uint64_t cmb_nodeswitch_port_queue_min(const struct cmb_nodeswitch *nsp,
                                                      uint16_t port)
{
    if (port >= nsp->port_count) return 0;
    return nsp->ports[port].min_queue_depth;
}

static inline uint64_t cmb_nodeswitch_port_queue_max(const struct cmb_nodeswitch *nsp,
                                                      uint16_t port)
{
    if (port >= nsp->port_count) return 0;
    return nsp->ports[port].max_queue_depth;
}

static inline double cmb_nodeswitch_port_queue_avg(const struct cmb_nodeswitch *nsp,
                                                    uint16_t port)
{
    if (port >= nsp->port_count) return 0.0;
    if (nsp->ports[port].queue_samples == 0) return 0.0;
    return (double)nsp->ports[port].total_queue_depth / (double)nsp->ports[port].queue_samples;
}

static inline uint64_t cmb_nodeswitch_port_packets_ecn_marked(const struct cmb_nodeswitch *nsp,
                                                               uint16_t port)
{
    if (port >= nsp->port_count) return 0;
    return nsp->ports[port].packets_ecn_marked;
}

/**
 * @brief Get total packets processed by this switch.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @return Number of packets processed.
 */
static inline uint64_t cmb_nodeswitch_packets_processed(const struct cmb_nodeswitch *nsp)
{
    return nsp->packets_processed;
}

/**
 * @brief Get total packets dropped due to no route.
 *
 * @memberof cmb_nodeswitch
 * @param nsp Pointer to a nodeswitch.
 * @return Number of packets dropped.
 */
static inline uint64_t cmb_nodeswitch_packets_dropped_no_route(const struct cmb_nodeswitch *nsp)
{
    return nsp->packets_dropped_no_route;
}

static inline uint64_t cmb_nodeswitch_packets_dropped_overflow(const struct cmb_nodeswitch *nsp)
{
    return nsp->packets_dropped_overflow;
}

#endif /* CIMBA_CMB_NODESWITCH_H */