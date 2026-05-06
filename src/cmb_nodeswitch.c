/**
 * @file cmb_nodeswitch.c
 * @brief Network switch node implementation.
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

#include "cmb_nodeswitch.h"
#include "cmb_packet.h"
#include "cmb_event.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"

static void *nodeswitch_processor_proc(struct cmb_process *proc, void *context);

struct cmb_nodeswitch *cmb_nodeswitch_create(void)
{
    struct cmb_nodeswitch *nsp = cmi_malloc(sizeof(*nsp));
    cmi_memset(nsp, 0, sizeof(*nsp));
    ((struct cmi_resourcebase *)nsp)->cookie = CMI_UNINITIALIZED;

    return nsp;
}

void cmb_nodeswitch_initialize(struct cmb_nodeswitch *nsp,
                               const char *name,
                               uint16_t port_count,
                               const struct cmb_nodeswitch_port_config *port_configs)
{
    cmb_assert_release(nsp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(port_count > 0);
    cmb_assert_release(port_count <= CMB_NODESWITCH_MAX_PORTS);
    cmb_assert_release(port_configs != NULL);

    cmi_resourcebase_initialize(&nsp->core, name);

    nsp->port_count = port_count;
    nsp->ports = cmi_malloc(port_count * sizeof(struct cmb_nodeswitch_port));
    cmi_memset(nsp->ports, 0, port_count * sizeof(struct cmb_nodeswitch_port));

    for (uint16_t i = 0; i < port_count; i++) {
        char queue_name[64];
        snprintf(queue_name, sizeof(queue_name), "%s-p%d-fifo", name, i);

        uint64_t q_depth = (port_configs[i].queue_depth > 0) ?
                           port_configs[i].queue_depth : 64;
        enum cmb_queue_type q_type = port_configs[i].queue_type;
        uint64_t p_speed = port_configs[i].port_speed_bits_per_sec;

        nsp->ports[i].fifo_queue = cmb_objectqueue_create();
        cmb_objectqueue_initialize(nsp->ports[i].fifo_queue,
                                    queue_name,
                                    q_depth);

        snprintf(queue_name, sizeof(queue_name), "%s-p%d-prio", name, i);
        nsp->ports[i].priority_queue = cmb_priorityqueue_create();
        cmb_priorityqueue_initialize(nsp->ports[i].priority_queue,
                                      queue_name,
                                      q_depth);

        nsp->ports[i].queue_depth = port_configs[i].queue_depth;
        nsp->ports[i].queue_type = port_configs[i].queue_type;
        nsp->ports[i].port_speed_bits_per_sec = port_configs[i].port_speed_bits_per_sec;
        nsp->ports[i].drop_on_overflow = port_configs[i].drop_on_overflow;
        nsp->ports[i].enable_ecn = port_configs[i].enable_ecn;
        nsp->ports[i].ecn_kmin = port_configs[i].ecn_kmin;
        nsp->ports[i].ecn_kmax = port_configs[i].ecn_kmax;
        nsp->ports[i].link_out = NULL;
        nsp->ports[i].link_out_port = 0;
        nsp->ports[i].packets_received = 0;
        nsp->ports[i].packets_sent = 0;
        nsp->ports[i].packets_dropped_overflow = 0;
        nsp->ports[i].packets_ecn_marked = 0;
        nsp->ports[i].current_queue_depth = 0;
        nsp->ports[i].min_queue_depth = 0;
        nsp->ports[i].max_queue_depth = 0;
        nsp->ports[i].total_queue_depth = 0;
        nsp->ports[i].queue_samples = 0;
    }

    nsp->input_queue = cmb_objectqueue_create();
    cmb_objectqueue_initialize(nsp->input_queue, name, UINT64_MAX);

    nsp->local_delivery_queue = cmb_objectqueue_create();
    cmb_objectqueue_initialize(nsp->local_delivery_queue, name, UINT64_MAX);
    nsp->local_addr = 0;

    nsp->routing_table_capacity = 16;
    nsp->routing_table_size = 0;
    nsp->routing_table = cmi_malloc(nsp->routing_table_capacity *
                                    sizeof(struct cmb_nodeswitch_route));
    cmi_memset(nsp->routing_table, 0,
               nsp->routing_table_capacity * sizeof(struct cmb_nodeswitch_route));

    nsp->processor = cmb_process_create();
    nsp->is_running = false;
    nsp->packets_processed = 0;
    nsp->packets_dropped_no_route = 0;
    nsp->packets_dropped_overflow = 0;
}

void cmb_nodeswitch_terminate(struct cmb_nodeswitch *nsp)
{
    cmb_assert_release(nsp != NULL);

    if (nsp->is_running) {
        cmb_nodeswitch_stop(nsp);
    }

    for (uint16_t i = 0; i < nsp->port_count; i++) {
        cmb_objectqueue_terminate(nsp->ports[i].fifo_queue);
        cmb_objectqueue_destroy(nsp->ports[i].fifo_queue);
        cmb_priorityqueue_terminate(nsp->ports[i].priority_queue);
        cmb_priorityqueue_destroy(nsp->ports[i].priority_queue);
    }

    cmi_free(nsp->ports);

    cmb_objectqueue_terminate(nsp->input_queue);
    cmb_objectqueue_destroy(nsp->input_queue);

    cmb_objectqueue_terminate(nsp->local_delivery_queue);
    cmb_objectqueue_destroy(nsp->local_delivery_queue);

    cmi_free(nsp->routing_table);

    if (nsp->processor != NULL) {
        cmb_process_terminate(nsp->processor);
        cmb_process_destroy(nsp->processor);
        nsp->processor = NULL;
    }

    cmi_resourcebase_terminate(&nsp->core);
}

void cmb_nodeswitch_destroy(struct cmb_nodeswitch *nsp)
{
    cmb_assert_release(nsp != NULL);

    cmb_nodeswitch_terminate(nsp);
    cmi_free(nsp);
}

void cmb_nodeswitch_set_port_link(struct cmb_nodeswitch *nsp,
                                   uint16_t port,
                                   struct cmb_link *link,
                                   uint16_t link_port)
{
    cmb_assert_release(nsp != NULL);
    cmb_assert_release(port < nsp->port_count);
    cmb_assert_release(link != NULL);

    nsp->ports[port].link_out = link;
    nsp->ports[port].link_out_port = link_port;
}

void cmb_nodeswitch_route_add(struct cmb_nodeswitch *nsp,
                               uint32_t dest_addr,
                               uint16_t out_port,
                               struct cmb_link *link)
{
    cmb_assert_release(nsp != NULL);
    cmb_assert_release(out_port < nsp->port_count);

    if (nsp->routing_table_size >= nsp->routing_table_capacity) {
        uint16_t new_capacity = nsp->routing_table_capacity * 2;
        struct cmb_nodeswitch_route *new_table =
            cmi_malloc(new_capacity * sizeof(struct cmb_nodeswitch_route));
        cmi_memcpy(new_table, nsp->routing_table,
                   nsp->routing_table_size * sizeof(struct cmb_nodeswitch_route));
        cmi_free(nsp->routing_table);
        nsp->routing_table = new_table;
        nsp->routing_table_capacity = new_capacity;
    }

    struct cmb_nodeswitch_route *entry = &nsp->routing_table[nsp->routing_table_size++];
    entry->dest_addr = dest_addr;
    entry->out_port = out_port;
    entry->link = link;
}

bool cmb_nodeswitch_route_lookup(struct cmb_nodeswitch *nsp,
                                  uint32_t dest_addr,
                                  uint16_t *out_port,
                                  struct cmb_link **link)
{
    cmb_assert_release(nsp != NULL);

    for (uint16_t i = 0; i < nsp->routing_table_size; i++) {
        if (nsp->routing_table[i].dest_addr == dest_addr) {
            *out_port = nsp->routing_table[i].out_port;
            *link = nsp->routing_table[i].link;
            return true;
        }
    }

    return false;
}

void cmb_nodeswitch_set_local_addr(struct cmb_nodeswitch *nsp, uint32_t addr)
{
    cmb_assert_release(nsp != NULL);
    nsp->local_addr = addr;
}

struct cmb_objectqueue *cmb_nodeswitch_get_local_delivery_queue(struct cmb_nodeswitch *nsp)
{
    cmb_assert_release(nsp != NULL);
    return nsp->local_delivery_queue;
}

void cmb_nodeswitch_enqueue(struct cmb_nodeswitch *nsp, void *packet)
{
    cmb_assert_release(nsp != NULL);
    cmb_assert_release(packet != NULL);

    cmb_objectqueue_put(nsp->input_queue, packet);
}

static void *nodeswitch_processor_proc(struct cmb_process *proc, void *context)
{
    (void)context;

    struct cmb_nodeswitch *nsp = (struct cmb_nodeswitch *)cmi_coroutine_context((struct cmi_coroutine *)proc);
    cmb_assert_release(nsp != NULL);

    while (true) {
        void *packet = NULL;
        int64_t result = cmb_objectqueue_get(nsp->input_queue, &packet);

        if (result != CMB_PROCESS_SUCCESS) {
            break;
        }

        struct cmb_packet *pkt = (struct cmb_packet *)packet;

        pkt->port_in = pkt->port_in;

        if (nsp->local_addr != 0 && pkt->dst_addr == nsp->local_addr) {
            cmb_objectqueue_put(nsp->local_delivery_queue, pkt);
            nsp->packets_processed++;
            continue;
        }

        uint16_t out_port;
        struct cmb_link *out_link = NULL;

        if (!cmb_nodeswitch_route_lookup(nsp, pkt->dst_addr, &out_port, &out_link)) {
            pkt->stats.drop_time = cmb_time();
            nsp->packets_dropped_no_route++;
            cmb_process_yield();
            continue;
        }

        struct cmb_nodeswitch_port *port = &nsp->ports[out_port];

        uint64_t q_len = 0;
        bool queue_full = false;

        if (port->queue_type == CMB_QUEUE_PRIORITY) {
            q_len = cmb_priorityqueue_length(port->priority_queue);
            queue_full = (q_len >= port->queue_depth);
        } else {
            q_len = cmb_objectqueue_length(port->fifo_queue);
            queue_full = (q_len >= port->queue_depth);
        }

        if (queue_full && port->drop_on_overflow) {
            port->packets_dropped_overflow++;
            nsp->packets_dropped_overflow++;
            cmi_free(pkt);
            continue;
        }

        if (port->queue_type == CMB_QUEUE_PRIORITY) {
            int64_t priority = cmb_packet_queue_priority(pkt);
            uint64_t handle;
            cmb_priorityqueue_put(port->priority_queue, pkt, priority, &handle);
            q_len = cmb_priorityqueue_length(port->priority_queue);
        } else {
            cmb_objectqueue_put(port->fifo_queue, pkt);
            q_len = cmb_objectqueue_length(port->fifo_queue);
        }

        if (q_len > port->max_queue_depth) port->max_queue_depth = q_len;
        if (port->min_queue_depth == 0 || q_len < port->min_queue_depth) port->min_queue_depth = q_len;
        port->current_queue_depth = q_len;
        port->total_queue_depth += q_len;
        port->queue_samples++;

        if (port->enable_ecn && q_len >= port->ecn_kmin && !cmb_packet_is_ce_marked(pkt)) {
            if (port->ecn_kmax == 0 || q_len >= port->ecn_kmax) {
                cmb_packet_mark_ce(pkt);
                port->packets_ecn_marked++;
            }
        }

        port->packets_received++;
        nsp->packets_processed++;
    }

    return NULL;
}

void cmb_nodeswitch_start(struct cmb_nodeswitch *nsp)
{
    cmb_assert_release(nsp != NULL);
    cmb_assert_release(!nsp->is_running);

    cmb_process_initialize(nsp->processor,
                           "switch-processor",
                           nodeswitch_processor_proc,
                           nsp,
                           0);

    nsp->is_running = true;
    cmb_process_start(nsp->processor);
}

void cmb_nodeswitch_stop(struct cmb_nodeswitch *nsp)
{
    cmb_assert_release(nsp != NULL);

    if (!nsp->is_running) {
        return;
    }

    nsp->is_running = false;

    if (nsp->processor != NULL &&
        cmb_process_status(nsp->processor) != CMB_PROCESS_FINISHED) {
        cmb_process_stop(nsp->processor, NULL);
    }
}

void *cmb_nodeswitch_get_port_queue(struct cmb_nodeswitch *nsp, uint16_t port)
{
    cmb_assert_release(nsp != NULL);
    cmb_assert_release(port < nsp->port_count);

    if (nsp->ports[port].queue_type == CMB_QUEUE_PRIORITY) {
        return nsp->ports[port].priority_queue;
    }
    return nsp->ports[port].fifo_queue;
}