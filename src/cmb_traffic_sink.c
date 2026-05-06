/**
 * @file cmb_traffic_sink.c
 * @brief Traffic sink implementation.
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
#include <math.h>

#include "cmb_traffic_sink.h"
#include "cmb_packet.h"
#include "cmb_event.h"
#include "cmb_nodeswitch.h"

static void *traffic_sink_proc(struct cmb_process *proc, void *context);

struct cmb_traffic_sink *cmb_traffic_sink_create(void)
{
    struct cmb_traffic_sink *ts = cmi_malloc(sizeof(*ts));
    cmi_memset(ts, 0, sizeof(*ts));

    return ts;
}

void cmb_traffic_sink_initialize(struct cmb_traffic_sink *ts,
                                   const char *name,
                                   struct cmb_nodeswitch *src_switch,
                                   uint32_t listen_addr)
{
    cmb_assert_release(ts != NULL);
    cmb_assert_release(src_switch != NULL);

    ts->src_switch = src_switch;
    ts->listen_addr = listen_addr;
    ts->running = false;

    memset(&ts->stats, 0, sizeof(ts->stats));
    cmb_datasummary_initialize(&ts->stats.delay_summary);

    (void)cmb_process_name_set(&ts->proc, name);
    ts->proc.priority = 0;
}

void cmb_traffic_sink_terminate(struct cmb_traffic_sink *ts)
{
    cmb_assert_release(ts != NULL);

    if (ts->running) {
        cmb_traffic_sink_stop(ts);
    }

    ts->src_switch = NULL;
}

void cmb_traffic_sink_destroy(struct cmb_traffic_sink *ts)
{
    cmb_assert_release(ts != NULL);

    cmb_traffic_sink_terminate(ts);
    cmi_free(ts);
}

void cmb_traffic_sink_start(struct cmb_traffic_sink *ts)
{
    cmb_assert_release(ts != NULL);
    cmb_assert_release(!ts->running);

    ts->running = true;

    cmb_process_initialize(&ts->proc,
                           "traffic-sink",
                           traffic_sink_proc,
                           ts,
                           0);
    cmb_process_start(&ts->proc);
}

void cmb_traffic_sink_stop(struct cmb_traffic_sink *ts)
{
    cmb_assert_release(ts != NULL);

    if (!ts->running) {
        return;
    }

    ts->running = false;

    if (cmb_process_status(&ts->proc) != CMB_PROCESS_FINISHED) {
        cmb_process_stop(&ts->proc, NULL);
    }
}

static void *traffic_sink_proc(struct cmb_process *proc, void *context)
{
    (void)proc;
    struct cmb_traffic_sink *ts = (struct cmb_traffic_sink *)context;

    struct cmb_objectqueue *queue = cmb_nodeswitch_get_local_delivery_queue(ts->src_switch);

    while (ts->running) {
        void *item = NULL;
        int64_t result = cmb_objectqueue_get(queue, &item);

        if (result != CMB_PROCESS_SUCCESS) {
            break;
        }

        struct cmb_packet *pkt = (struct cmb_packet *)item;

        if (ts->listen_addr == 0 || pkt->dst_addr == ts->listen_addr) {
            double now = cmb_time();
            double delay_ns = (now - pkt->creation_time) * 1e9;

            ts->stats.packets_received++;
            ts->stats.bytes_received += pkt->size_bits / 8;
            ts->stats.total_delay_ns += delay_ns;

            if (ts->stats.packets_received == 1) {
                ts->stats.min_delay_ns = delay_ns;
                ts->stats.max_delay_ns = delay_ns;
            } else {
                if (delay_ns < ts->stats.min_delay_ns) {
                    ts->stats.min_delay_ns = delay_ns;
                }
                if (delay_ns > ts->stats.max_delay_ns) {
                    ts->stats.max_delay_ns = delay_ns;
                }
            }

            cmb_datasummary_add(&ts->stats.delay_summary, delay_ns);

            uint32_t qos_idx = (uint32_t)pkt->qos;
            if (qos_idx < CMB_QOS_LEVELS) {
                struct cmb_qos_stats *qs = &ts->stats.qos_stats[qos_idx];
                qs->packets_received++;
                qs->bytes_received += pkt->size_bits / 8;
                qs->total_delay_ns += delay_ns;
                if (qs->packets_received == 1) {
                    qs->min_delay_ns = delay_ns;
                    qs->max_delay_ns = delay_ns;
                } else {
                    if (delay_ns < qs->min_delay_ns) qs->min_delay_ns = delay_ns;
                    if (delay_ns > qs->max_delay_ns) qs->max_delay_ns = delay_ns;
                }
                if (cmb_packet_was_ce_marked(pkt)) {
                    qs->ce_marked_count++;
                }
            }
        }

        cmi_free(pkt);
    }

    ts->running = false;
    return NULL;
}

void cmb_traffic_sink_print_stats(const struct cmb_traffic_sink *ts, FILE *fp)
{
    cmb_assert_release(ts != NULL);
    cmb_assert_release(fp != NULL);

    fprintf(fp, "\n=== Traffic Sink Statistics ===\n");
    fprintf(fp, "Packets Received: %lu\n", ts->stats.packets_received);
    fprintf(fp, "Bytes Received:   %lu\n", ts->stats.bytes_received);

    if (ts->stats.packets_received > 0) {
        double avg_ms = cmb_traffic_sink_avg_delay_ns(ts) / 1e6;
        double min_ms = ts->stats.min_delay_ns / 1e6;
        double max_ms = ts->stats.max_delay_ns / 1e6;
        fprintf(fp, "Avg Delay:        %.3f ms\n", avg_ms);
        fprintf(fp, "Min Delay:        %.3f ms\n", min_ms);
        fprintf(fp, "Max Delay:        %.3f ms\n", max_ms);
    }

    fprintf(fp, "\n--- Per-QoS Statistics ---\n");
    static const char *qos_names[CMB_QOS_LEVELS] = {
        "BEST_EFFORT", "PRIORITY", "VIDEO", "VOICE", "CONTROL"
    };

    for (uint32_t i = 0; i < CMB_QOS_LEVELS; i++) {
        struct cmb_qos_stats *qs = &ts->stats.qos_stats[i];
        if (qs->packets_received > 0) {
            double avg_ms = qs->total_delay_ns / qs->packets_received / 1e6;
            fprintf(fp, "QoS %d (%s): pkts=%lu bytes=%lu avg_delay=%.3fms",
                    i, qos_names[i], qs->packets_received, qs->bytes_received, avg_ms);
            if (qs->ce_marked_count > 0) {
                fprintf(fp, " ce_marked=%lu", qs->ce_marked_count);
            }
            fprintf(fp, "\n");
        }
    }
}