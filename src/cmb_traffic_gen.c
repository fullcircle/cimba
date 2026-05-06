/**
 * @file cmb_traffic_gen.c
 * @brief Traffic generator implementation.
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

#include "cmb_traffic_gen.h"
#include "cmb_packet.h"
#include "cmb_event.h"
#include "cmb_network.h"

static void *traffic_gen_proc(struct cmb_process *proc, void *context);

struct cmb_traffic_gen *cmb_traffic_gen_create(void)
{
    struct cmb_traffic_gen *tg = cmi_malloc(sizeof(*tg));
    cmi_memset(tg, 0, sizeof(*tg));

    return tg;
}

void cmb_traffic_gen_initialize(struct cmb_traffic_gen *tg,
                                  const char *name,
                                  struct cmb_nodeswitch *dst_switch,
                                  const struct cmb_traffic_gen_config *config,
                                  struct cmb_random *rng)
{
    cmb_assert_release(tg != NULL);
    cmb_assert_release(dst_switch != NULL);
    cmb_assert_release(config != NULL);

    tg->dst_switch = dst_switch;
    tg->config = *config;
    tg->packets_generated = 0;
    tg->running = false;
    tg->rng = rng;
    tg->net = NULL;
    tg->congestion_factor = 1.0;
    tg->last_ce_count = 0;
    tg->packets_since_feedback = 0;

    (void)cmb_process_name_set(&tg->proc, name);
    tg->proc.priority = 0;
}

void cmb_traffic_gen_terminate(struct cmb_traffic_gen *tg)
{
    cmb_assert_release(tg != NULL);

    if (tg->running) {
        cmb_traffic_gen_stop(tg);
    }

    tg->dst_switch = NULL;
}

void cmb_traffic_gen_destroy(struct cmb_traffic_gen *tg)
{
    cmb_assert_release(tg != NULL);

    cmb_traffic_gen_terminate(tg);
    cmi_free(tg);
}

void cmb_traffic_gen_set_network(struct cmb_traffic_gen *tg, struct cmb_network *net)
{
    cmb_assert_release(tg != NULL);
    tg->net = net;
}

double cmb_traffic_gen_get_congestion_factor(const struct cmb_traffic_gen *tg)
{
    cmb_assert_release(tg != NULL);
    return tg->congestion_factor;
}

void cmb_traffic_gen_start(struct cmb_traffic_gen *tg)
{
    cmb_assert_release(tg != NULL);
    cmb_assert_release(!tg->running);

    tg->running = true;

    cmb_process_initialize(&tg->proc,
                           "traffic-gen",
                           traffic_gen_proc,
                           tg,
                           0);
    cmb_process_start(&tg->proc);
}

void cmb_traffic_gen_stop(struct cmb_traffic_gen *tg)
{
    cmb_assert_release(tg != NULL);

    if (!tg->running) {
        return;
    }

    tg->running = false;

    if (cmb_process_status(&tg->proc) != CMB_PROCESS_FINISHED) {
        cmb_process_stop(&tg->proc, NULL);
    }
}

static struct cmb_packet *create_packet(struct cmb_traffic_gen *tg)
{
    struct cmb_packet *pkt = cmi_malloc(sizeof(*pkt));
    cmi_memset(pkt, 0, sizeof(*pkt));

    pkt->src_addr = tg->config.src_addr;
    pkt->dst_addr = tg->config.dst_addr;
    pkt->size_bits = tg->config.packet_size_bits;
    pkt->ttl = 64;
    pkt->qos = tg->config.qos;
    pkt->vlan_id = tg->config.vlan_id;
    pkt->vlan_pcp = tg->config.vlan_pcp;
    pkt->creation_time = cmb_time();

    return pkt;
}

static void *traffic_gen_proc(struct cmb_process *proc, void *context)
{
    (void)proc;
    struct cmb_traffic_gen *tg = (struct cmb_traffic_gen *)context;
    struct cmb_traffic_gen_config *cfg = &tg->config;

    double mean_interval = 1.0 / cfg->packet_rate_hz;

    uint64_t target = cfg->num_packets;
    if (target == 0) {
        target = UINT64_MAX;
    }

    uint64_t count = 0;
    uint64_t feedback_interval = 20;

    while (tg->running && count < target) {
        struct cmb_packet *pkt = create_packet(tg);

        cmb_nodeswitch_enqueue(tg->dst_switch, pkt);

        count++;
        tg->packets_generated++;
        tg->packets_since_feedback++;

        if (cfg->enable_ecn_feedback && tg->net != NULL && tg->packets_since_feedback >= feedback_interval) {
            uint64_t current_ce = cmb_network_total_ce_marked(tg->net);
            uint64_t delta_ce = (current_ce > tg->last_ce_count) ? (current_ce - tg->last_ce_count) : 0;
            double ce_rate = (double)delta_ce / (double)feedback_interval;

            if (ce_rate > cfg->ecn_feedback_threshold) {
                tg->congestion_factor *= 1.2;
                if (tg->congestion_factor > 10.0) {
                    tg->congestion_factor = 10.0;
                }
            } else {
                tg->congestion_factor *= 0.95;
                if (tg->congestion_factor < 1.0) {
                    tg->congestion_factor = 1.0;
                }
            }

            tg->last_ce_count = current_ce;
            tg->packets_since_feedback = 0;
        }

        double interval;
        switch (cfg->pattern) {
        case CMB_TRAFFIC_POISSON:
            interval = cmb_random_exponential(mean_interval) * tg->congestion_factor;
            break;
        case CMB_TRAFFIC_BURSTY:
            {
                double r = cmb_random_uniform(0.0, 1.0);
                if (r < cfg->burst_scale) {
                    interval = cfg->burst_duration_sec;
                } else {
                    interval = mean_interval * tg->congestion_factor;
                }
            }
            break;
        case CMB_TRAFFIC_CONSTANT:
        default:
            interval = mean_interval * tg->congestion_factor;
            break;
        }

        if (count < target) {
            cmb_process_hold(interval);
        }
    }

    tg->running = false;
    return NULL;
}