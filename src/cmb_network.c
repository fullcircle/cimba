/**
 * @file cmb_network.c
 * @brief Network container implementation.
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

#include "cmb_network.h"
#include "cmb_packet.h"
#include "cmb_event.h"
#include "cmb_traffic_sink.h"

struct tx_worker_ctx {
    struct cmb_network *net;
    struct cmb_nodeswitch *sw;
    uint16_t port;
};

static void end_sim_event(void *subject, void *object)
{
    (void)subject;
    (void)object;
}

static void *tx_worker_proc(struct cmb_process *proc, void *context)
{
    (void)proc;
    struct tx_worker_ctx *ctx = (struct tx_worker_ctx *)context;
    struct cmb_network *net = ctx->net;
    struct cmb_nodeswitch *sw = ctx->sw;
    uint16_t port_idx = ctx->port;

    struct cmb_nodeswitch_port *port = &sw->ports[port_idx];

    while (true) {
        void *pkt_or_handle = NULL;
        int64_t result;

        if (port->queue_type == CMB_QUEUE_PRIORITY) {
            result = cmb_priorityqueue_get(port->priority_queue, &pkt_or_handle);
        } else {
            result = cmb_objectqueue_get(port->fifo_queue, &pkt_or_handle);
        }

        if (result != CMB_PROCESS_SUCCESS) {
            break;
        }

        struct cmb_packet *pkt = (struct cmb_packet *)pkt_or_handle;

        if (port->link_out == NULL) {
            port->packets_dropped_overflow++;
            net->stats.total_packets_dropped++;
            continue;
        }

        uint32_t size_bits = pkt->size_bits;
        result = cmb_link_transmit(port->link_out, size_bits);

        if (result == CMB_PROCESS_SUCCESS) {
            port->packets_sent++;
            net->stats.total_packets_transmitted++;

            struct cmb_nodeswitch *dst_sw = (struct cmb_nodeswitch *)port->link_out->dst_node;
            if (dst_sw != NULL) {
                pkt->port_in = port_idx;
                pkt->port_out = port->link_out_port;
                cmb_nodeswitch_enqueue(dst_sw, pkt);
            }
        } else {
            port->packets_dropped_overflow++;
            net->stats.total_packets_dropped++;
        }
    }

    return NULL;
}

struct cmb_network *cmb_network_create(void)
{
    struct cmb_network *net = cmi_malloc(sizeof(*net));
    cmi_memset(net, 0, sizeof(*net));

    return net;
}

void cmb_network_initialize(struct cmb_network *net,
                              const struct cmb_network_config *config)
{
    cmb_assert_release(net != NULL);

    if (config != NULL) {
        net->stats_level = config->stats_level;
    } else {
        net->stats_level = CMB_NETWORK_STATS_AGGREGATE;
    }

    if (config != NULL && config->rng_seed != 0) {
        cmb_random_initialize(config->rng_seed);
    }

    net->switch_count = 0;
    net->link_count = 0;
    net->tx_workers = NULL;
    net->tx_worker_count = 0;
}

void cmb_network_terminate(struct cmb_network *net)
{
    cmb_assert_release(net != NULL);

    cmb_network_stop(net);

    for (uint16_t i = 0; i < net->switch_count; i++) {
        if (net->switches[i] != NULL) {
            cmb_nodeswitch_destroy(net->switches[i]);
            net->switches[i] = NULL;
        }
    }
    net->switch_count = 0;

    for (uint16_t i = 0; i < net->link_count; i++) {
        if (net->links[i] != NULL) {
            cmb_link_destroy(net->links[i]);
            net->links[i] = NULL;
        }
    }
    net->link_count = 0;
}

void cmb_network_destroy(struct cmb_network *net)
{
    cmb_assert_release(net != NULL);

    cmb_network_terminate(net);
    cmi_free(net);
}

bool cmb_network_add_switch(struct cmb_network *net, struct cmb_nodeswitch *sw)
{
    cmb_assert_release(net != NULL);
    cmb_assert_release(sw != NULL);

    if (net->switch_count >= CMB_NETWORK_MAX_SWITCHES) {
        return false;
    }

    net->switches[net->switch_count++] = sw;
    return true;
}

bool cmb_network_add_link(struct cmb_network *net, struct cmb_link *link)
{
    cmb_assert_release(net != NULL);
    cmb_assert_release(link != NULL);

    if (net->link_count >= CMB_NETWORK_MAX_LINKS) {
        return false;
    }

    net->links[net->link_count++] = link;
    return true;
}

bool cmb_network_add_traffic_sink(struct cmb_network *net, struct cmb_traffic_sink *ts)
{
    cmb_assert_release(net != NULL);
    cmb_assert_release(ts != NULL);

    if (net->traffic_sink_count >= CMB_NETWORK_MAX_TRAFFIC_SINKS) {
        return false;
    }

    net->traffic_sinks[net->traffic_sink_count++] = ts;
    return true;
}

void cmb_network_start(struct cmb_network *net)
{
    cmb_assert_release(net != NULL);

    uint16_t total_workers = 0;
    for (uint16_t i = 0; i < net->switch_count; i++) {
        total_workers += net->switches[i]->port_count;
    }

    net->tx_workers = cmi_malloc(total_workers * sizeof(struct cmb_process *));
    net->tx_worker_count = 0;

    for (uint16_t i = 0; i < net->switch_count; i++) {
        struct cmb_nodeswitch *sw = net->switches[i];
        cmb_nodeswitch_start(sw);

        for (uint16_t p = 0; p < sw->port_count; p++) {
            struct tx_worker_ctx *ctx = cmi_malloc(sizeof(*ctx));
            ctx->net = net;
            ctx->sw = sw;
            ctx->port = p;

            struct cmb_process *worker = cmb_process_create();
            cmb_process_initialize(worker, "tx-worker", tx_worker_proc, ctx, 0);
            cmb_process_start(worker);

            net->tx_workers[net->tx_worker_count++] = worker;
        }
    }
}

void cmb_network_stop(struct cmb_network *net)
{
    cmb_assert_release(net != NULL);

    for (uint16_t i = 0; i < net->switch_count; i++) {
        if (net->switches[i] != NULL) {
            cmb_nodeswitch_stop(net->switches[i]);
        }
    }

    for (uint16_t i = 0; i < net->tx_worker_count; i++) {
        if (net->tx_workers[i] != NULL &&
            cmb_process_status(net->tx_workers[i]) != CMB_PROCESS_FINISHED) {
            cmb_process_stop(net->tx_workers[i], NULL);
        }
    }

    for (uint16_t i = 0; i < net->tx_worker_count; i++) {
        if (net->tx_workers[i] != NULL) {
            cmb_process_terminate(net->tx_workers[i]);
            cmb_process_destroy(net->tx_workers[i]);
            net->tx_workers[i] = NULL;
        }
    }

    if (net->tx_workers != NULL) {
        cmi_free(net->tx_workers);
        net->tx_workers = NULL;
    }
    net->tx_worker_count = 0;
}

void cmb_network_run(struct cmb_network *net, double duration)
{
    cmb_assert_release(net != NULL);

    double end_time = cmb_time() + duration;
    cmb_event_schedule(end_sim_event, NULL, NULL, end_time, 0);

    cmb_event_queue_execute();
}

struct cmb_nodeswitch *cmb_network_get_switch(const struct cmb_network *net, uint16_t index)
{
    if (index >= net->switch_count) {
        return NULL;
    }
    return net->switches[index];
}

struct cmb_link *cmb_network_get_link(const struct cmb_network *net, uint16_t index)
{
    if (index >= net->link_count) {
        return NULL;
    }
    return net->links[index];
}

uint64_t cmb_network_packets_transmitted(const struct cmb_network *net)
{
    uint64_t total = 0;
    for (uint16_t i = 0; i < net->link_count; i++) {
        total += cmb_link_packets_transmitted(net->links[i]);
    }
    return total;
}

uint64_t cmb_network_packets_dropped(const struct cmb_network *net)
{
    uint64_t total = 0;

    for (uint16_t i = 0; i < net->switch_count; i++) {
        total += cmb_nodeswitch_packets_dropped_no_route(net->switches[i]);
        for (uint16_t p = 0; p < net->switches[i]->port_count; p++) {
            total += net->switches[i]->ports[p].packets_dropped_overflow;
        }
    }

    for (uint16_t i = 0; i < net->link_count; i++) {
        total += cmb_link_packets_dropped(net->links[i]);
    }

    return total;
}

uint64_t cmb_network_packets_delivered(const struct cmb_network *net)
{
    uint64_t total = 0;
    for (uint16_t i = 0; i < net->traffic_sink_count; i++) {
        total += cmb_traffic_sink_packets_received(net->traffic_sinks[i]);
    }
    return total;
}

double cmb_network_avg_delay_ns(const struct cmb_network *net)
{
    uint64_t total_delivered = cmb_network_packets_delivered(net);
    if (total_delivered == 0) {
        return 0.0;
    }

    double total_delay_ns = 0.0;
    uint64_t total_bytes = 0;
    for (uint16_t i = 0; i < net->traffic_sink_count; i++) {
        total_delay_ns += cmb_traffic_sink_total_delay_ns(net->traffic_sinks[i]);
        total_bytes += cmb_traffic_sink_bytes_received(net->traffic_sinks[i]);
    }
    (void)total_bytes;
    return total_delay_ns / (double)total_delivered;
}

uint64_t cmb_network_total_ce_marked(const struct cmb_network *net)
{
    uint64_t total = 0;
    for (uint16_t i = 0; i < net->switch_count; i++) {
        struct cmb_nodeswitch *sw = net->switches[i];
        for (uint16_t p = 0; p < sw->port_count; p++) {
            total += cmb_nodeswitch_port_packets_ecn_marked(sw, p);
        }
    }
    return total;
}

void cmb_network_print_stats(const struct cmb_network *net, FILE *fp)
{
    cmb_assert_release(net != NULL);
    cmb_assert_release(fp != NULL);

    fprintf(fp, "\n=== Network Statistics ===\n");
    fprintf(fp, "Switches: %u\n", net->switch_count);
    fprintf(fp, "Links:     %u\n", net->link_count);
    fprintf(fp, "TX Workers: %u\n", net->tx_worker_count);
    fprintf(fp, "\n");
    fprintf(fp, "Packets Sent (Generated): %lu\n",
            cmb_network_packets_dropped(net) + cmb_network_packets_delivered(net));
    fprintf(fp, "Packets Transmitted: %lu\n", cmb_network_packets_transmitted(net));
    fprintf(fp, "Packets Dropped:     %lu\n", cmb_network_packets_dropped(net));
    fprintf(fp, "Packets Delivered:   %lu\n", cmb_network_packets_delivered(net));
    fprintf(fp, "Avg Delay (ns):      %.2f\n", cmb_network_avg_delay_ns(net));
    fprintf(fp, "CE Marked:          %lu\n", cmb_network_total_ce_marked(net));

    fprintf(fp, "\n--- Per-Switch Stats ---\n");
    for (uint16_t i = 0; i < net->switch_count; i++) {
        struct cmb_nodeswitch *sw = net->switches[i];
        fprintf(fp, "%s:\n", ((struct cmi_resourcebase *)sw)->name);
        fprintf(fp, "  Processed:        %lu\n", cmb_nodeswitch_packets_processed(sw));
        fprintf(fp, "  Dropped (route):  %lu\n", cmb_nodeswitch_packets_dropped_no_route(sw));
        fprintf(fp, "  Dropped (overflow): %lu\n", sw->packets_dropped_overflow);
        for (uint16_t p = 0; p < sw->port_count; p++) {
            uint64_t q_min = cmb_nodeswitch_port_queue_min(sw, p);
            uint64_t q_max = cmb_nodeswitch_port_queue_max(sw, p);
            double q_avg = cmb_nodeswitch_port_queue_avg(sw, p);
            uint64_t ecn_marked = cmb_nodeswitch_port_packets_ecn_marked(sw, p);
            fprintf(fp, "  Port %u: rx=%lu tx=%lu dropped=%lu",
                    p,
                    cmb_nodeswitch_port_packets_received(sw, p),
                    cmb_nodeswitch_port_packets_sent(sw, p),
                    cmb_nodeswitch_port_packets_dropped(sw, p));
            if (ecn_marked > 0) {
                fprintf(fp, " ecn_marked=%lu", ecn_marked);
            }
            fprintf(fp, " q_depth=(%lu/%0.2f/%lu)\n", q_min, q_avg, q_max);
        }
    }

    fprintf(fp, "\n--- Per-Link Stats ---\n");
    for (uint16_t i = 0; i < net->link_count; i++) {
        struct cmb_link *link = net->links[i];
        fprintf(fp, "%s:\n", cmb_buffer_get_name(&link->tx_buffer));
        fprintf(fp, "  Transmitted: %lu\n", cmb_link_packets_transmitted(link));
        fprintf(fp, "  Bits:        %lu\n", cmb_link_bits_transmitted(link));
        fprintf(fp, "  Dropped:     %lu\n", cmb_link_packets_dropped(link));
    }
}