/**
 * @file cmb_link.c
 * @brief Point-to-point link implementation.
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

#include "cmb_link.h"
#include "cmb_event.h"
#include "cmb_logger.h"

#include "cmi_memutils.h"

struct cmb_link *cmb_link_create(void)
{
    struct cmb_link *lp = cmi_malloc(sizeof(*lp));
    cmi_memset(lp, 0, sizeof(*lp));
    ((struct cmi_resourcebase *)lp)->cookie = CMI_UNINITIALIZED;

    return lp;
}

void cmb_link_initialize(struct cmb_link *lp,
                          const char *name,
                          void *src_node,
                          uint16_t src_port,
                          void *dst_node,
                          uint16_t dst_port,
                          const struct cmb_link_config *config)
{
    cmb_assert_release(lp != NULL);
    cmb_assert_release(name != NULL);
    cmb_assert_release(config != NULL);
    cmb_assert_release(config->bandwidth_bits_per_sec > 0);

    cmi_resourcebase_initialize(&lp->core, name);

    uint64_t buffer_capacity = (config->buffer_capacity_bits > 0)
        ? config->buffer_capacity_bits
        : config->bandwidth_bits_per_sec;

    cmb_buffer_initialize(&lp->tx_buffer, name, buffer_capacity);

    lp->bandwidth_bits_per_sec = config->bandwidth_bits_per_sec;
    lp->propagation_delay_sec = config->propagation_delay_sec;

    lp->src_node = src_node;
    lp->dst_node = dst_node;
    lp->src_port = src_port;
    lp->dst_port = dst_port;

    lp->packets_transmitted = 0;
    lp->bits_transmitted = 0;
    lp->packets_dropped = 0;
}

void cmb_link_terminate(struct cmb_link *lp)
{
    cmb_assert_release(lp != NULL);

    cmb_buffer_terminate(&lp->tx_buffer);
    cmi_resourcebase_terminate(&lp->core);
}

void cmb_link_destroy(struct cmb_link *lp)
{
    cmb_assert_release(lp != NULL);

    cmb_link_terminate(lp);
    cmi_free(lp);
}

int64_t cmb_link_transmit(struct cmb_link *lp, uint32_t packet_size_bits)
{
    cmb_assert_release(lp != NULL);

    uint64_t bits = packet_size_bits;
    int64_t result;

    result = cmb_buffer_put(&lp->tx_buffer, &bits);
    if (result != CMB_PROCESS_SUCCESS) {
        lp->packets_dropped++;
        return result;
    }

    lp->bits_transmitted += packet_size_bits;

    double tx_time_sec = (double)packet_size_bits / (double)lp->bandwidth_bits_per_sec;
    cmb_process_hold(tx_time_sec);

    uint64_t drain_bits = packet_size_bits;
    result = cmb_buffer_get(&lp->tx_buffer, &drain_bits);
    if (result != CMB_PROCESS_SUCCESS) {
        return result;
    }

    if (lp->propagation_delay_sec > 0.0) {
        cmb_process_hold(lp->propagation_delay_sec);
    }

    lp->packets_transmitted++;

    return CMB_PROCESS_SUCCESS;
}