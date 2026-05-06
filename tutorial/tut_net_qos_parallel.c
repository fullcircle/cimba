/**
 * @file tut_net_qos_parallel.c
 * @brief Tutorial demonstrating parallel network simulation using cimba_run_experiment().
 *
 * This tutorial creates multiple network simulation trials and runs them in parallel
 * using the Cimba experiment framework. Each trial is an independent simulation
 * with its own parameters and results.
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

#include <cimba.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#define NUM_CONFIGS 4
#define NUM_REPS 3
#define TOTAL_TRIALS (NUM_CONFIGS * NUM_REPS)

struct network_trial {
    const char *name;
    uint64_t num_packets;
    double total_rate;
    double bandwidth_bps;
    double propagation_delay_sec;
    uint64_t queue_depth;
    int enable_qos;
    int enable_ecn;
    int enable_ecn_feedback;
    uint32_t ecn_kmin;
    uint32_t ecn_kmax;
    uint64_t rng_seed;

    uint64_t packets_sent;
    uint64_t packets_delivered;
    uint64_t packets_dropped;
    uint64_t ce_marked;
    double avg_delay_ns;
    double voip_delay_ms;
    double be_delay_ms;
    double congestion_factor;
};

static void end_sim(void *subject, void *object)
{
    (void)subject;
    (void)object;
}

static void run_network_trial(void *vtrl)
{
    struct network_trial *trl = (struct network_trial *)vtrl;

    double voip_rate = trl->total_rate * 0.2;
    double be_rate = trl->total_rate * 0.8;

    cmb_event_queue_initialize(0.0);
    cmb_random_initialize(trl->rng_seed);

    struct cmb_network_config net_config = {
        .stats_level = CMB_NETWORK_STATS_AGGREGATE,
        .rng_seed = 0
    };

    struct cmb_network *net = cmb_network_create();
    cmb_network_initialize(net, &net_config);

    enum cmb_queue_type queue_type = trl->enable_qos ? CMB_QUEUE_PRIORITY : CMB_QUEUE_FIFO;

    struct cmb_nodeswitch_port_config port_config = {
        .queue_depth = trl->queue_depth,
        .queue_type = queue_type,
        .port_speed_bits_per_sec = 1e9,
        .drop_on_overflow = true,
        .enable_ecn = trl->enable_ecn ? true : false,
        .ecn_kmin = trl->ecn_kmin,
        .ecn_kmax = trl->ecn_kmax
    };

    struct cmb_nodeswitch *sw1 = cmb_nodeswitch_create();
    cmb_nodeswitch_initialize(sw1, "Switch1", 2, &port_config);

    struct cmb_nodeswitch *sw2 = cmb_nodeswitch_create();
    cmb_nodeswitch_initialize(sw2, "Switch2", 2, &port_config);

    cmb_network_add_switch(net, sw1);
    cmb_network_add_switch(net, sw2);

    struct cmb_link_config link_config = {
        .bandwidth_bits_per_sec = trl->bandwidth_bps,
        .propagation_delay_sec = trl->propagation_delay_sec,
        .buffer_capacity_bits = trl->bandwidth_bps * 10
    };

    struct cmb_link *link1 = cmb_link_create();
    cmb_link_initialize(link1, "Link1-2", sw1, 0, sw2, 0, &link_config);

    cmb_network_add_link(net, link1);

    cmb_nodeswitch_set_port_link(sw1, 0, link1, 0);
    cmb_nodeswitch_set_port_link(sw2, 0, link1, 0);

    cmb_nodeswitch_route_add(sw1, 0x0A000002, 0, link1);
    cmb_nodeswitch_route_add(sw2, 0x0A000001, 0, link1);
    cmb_nodeswitch_set_local_addr(sw2, 0x0A000002);

    struct cmb_traffic_gen_config voip_config = {
        .src_addr = 0x0A000001,
        .dst_addr = 0x0A000002,
        .packet_rate_hz = voip_rate,
        .packet_size_bits = 256,
        .pattern = CMB_TRAFFIC_POISSON,
        .qos = CMB_QOS_VOICE,
        .vlan_id = 0,
        .vlan_pcp = 0,
        .num_packets = trl->num_packets,
        .burst_duration_sec = 0.0,
        .burst_scale = 0.0,
        .enable_ecn_feedback = trl->enable_ecn_feedback ? true : false,
        .ecn_feedback_threshold = 0.1
    };

    struct cmb_traffic_gen_config be_config = {
        .src_addr = 0x0A000001,
        .dst_addr = 0x0A000002,
        .packet_rate_hz = be_rate,
        .packet_size_bits = 1024,
        .pattern = CMB_TRAFFIC_POISSON,
        .qos = CMB_QOS_BEST_EFFORT,
        .vlan_id = 0,
        .vlan_pcp = 0,
        .num_packets = trl->num_packets,
        .burst_duration_sec = 0.0,
        .burst_scale = 0.0,
        .enable_ecn_feedback = trl->enable_ecn_feedback ? true : false,
        .ecn_feedback_threshold = 0.1
    };

    struct cmb_traffic_gen *voip_gen = cmb_traffic_gen_create();
    cmb_traffic_gen_initialize(voip_gen, "VoIP-Traffic", sw1, &voip_config, NULL);
    if (trl->enable_ecn_feedback) {
        cmb_traffic_gen_set_network(voip_gen, net);
    }

    struct cmb_traffic_gen *be_gen = cmb_traffic_gen_create();
    cmb_traffic_gen_initialize(be_gen, "BE-Traffic", sw1, &be_config, NULL);
    if (trl->enable_ecn_feedback) {
        cmb_traffic_gen_set_network(be_gen, net);
    }

    struct cmb_traffic_sink *sink = cmb_traffic_sink_create();
    cmb_traffic_sink_initialize(sink, "TrafficSink", sw2, 0x0A000002);
    cmb_network_add_traffic_sink(net, sink);

    cmb_network_start(net);
    cmb_traffic_gen_start(voip_gen);
    cmb_traffic_gen_start(be_gen);
    cmb_traffic_sink_start(sink);

    double sim_duration = (trl->num_packets * 2) / voip_rate + 0.5;
    cmb_event_schedule(end_sim, NULL, NULL, sim_duration, 0);
    cmb_event_queue_execute();

    cmb_traffic_gen_stop(voip_gen);
    cmb_traffic_gen_stop(be_gen);
    cmb_traffic_sink_stop(sink);
    cmb_network_stop(net);

    trl->packets_sent = cmb_traffic_gen_packets_sent(voip_gen) + cmb_traffic_gen_packets_sent(be_gen);
    trl->packets_delivered = cmb_network_packets_delivered(net);
    trl->packets_dropped = cmb_network_packets_dropped(net);
    trl->ce_marked = cmb_network_total_ce_marked(net);
    trl->avg_delay_ns = cmb_network_avg_delay_ns(net);

    struct cmb_qos_stats *voip_qos = &sink->stats.qos_stats[CMB_QOS_VOICE];
    struct cmb_qos_stats *be_qos = &sink->stats.qos_stats[CMB_QOS_BEST_EFFORT];
    trl->voip_delay_ms = (voip_qos->packets_received > 0) ?
        (voip_qos->total_delay_ns / voip_qos->packets_received / 1e6) : 0.0;
    trl->be_delay_ms = (be_qos->packets_received > 0) ?
        (be_qos->total_delay_ns / be_qos->packets_received / 1e6) : 0.0;

    trl->congestion_factor = cmb_traffic_gen_get_congestion_factor(voip_gen);

    cmb_traffic_gen_destroy(voip_gen);
    cmb_traffic_gen_destroy(be_gen);
    cmb_traffic_sink_destroy(sink);
    cmb_network_destroy(net);

    cmb_event_queue_terminate();
    cmb_random_terminate();
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -n, --num-pkts <N>       Packets per traffic class (default: 500)\n");
    printf("  -r, --rate <HZ>         Total packet rate in Hz (default: 5000)\n");
    printf("  -b, --bw <BPS>          Link bandwidth in bits/sec (default: 1e4)\n");
    printf("  -Q, --queue-depth <N>   Queue depth in packets (default: 100)\n");
    printf("  -s, --sequential         Run sequentially (default: parallel)\n");
    printf("  -h, --help              Show this help message\n");
    printf("\n");
    printf("This tutorial runs %d configurations x %d replications = %d trials\n",
           NUM_CONFIGS, NUM_REPS, TOTAL_TRIALS);
    printf("in parallel using cimba_run_experiment().\n");
}

int main(int argc, char *argv[])
{
    uint64_t num_packets = 500;
    double total_rate = 5000;
    double bandwidth_bps = 1e4;
    uint64_t queue_depth = 100;
    int sequential = 0;

    static struct option long_options[] = {
        {"num-pkts",  required_argument, 0, 'n'},
        {"rate",      required_argument, 0, 'r'},
        {"bw",        required_argument, 0, 'b'},
        {"queue-depth", required_argument, 0, 'Q'},
        {"sequential", no_argument,       0, 's'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:r:b:Q:sh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n':
                num_packets = atoll(optarg);
                break;
            case 'r':
                total_rate = atof(optarg);
                break;
            case 'b':
                bandwidth_bps = atof(optarg);
                break;
            case 'Q':
                queue_depth = atoll(optarg);
                break;
            case 's':
                sequential = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    printf("\n");
    printf("================================================================================\n");
    printf("        PARALLEL NETWORK SIMULATION - QoS AND ECN EXPERIMENT\n");
    printf("================================================================================\n");
    printf("\n");
    printf("Configurations:\n");
    printf("  1. PRIORITY queue, No ECN\n");
    printf("  2. PRIORITY queue, ECN marking\n");
    printf("  3. PRIORITY queue, ECN + feedback\n");
    printf("  4. FIFO queue, ECN marking\n");
    printf("\n");
    printf("Running %d configurations x %d replications = %d trials\n",
           NUM_CONFIGS, NUM_REPS, TOTAL_TRIALS);
    printf("using cimba_run_experiment() for parallel execution.\n");
    printf("\n");

    struct network_trial *experiment = calloc(TOTAL_TRIALS, sizeof(*experiment));

    uint64_t trial_idx = 0;
    for (int cfg = 0; cfg < NUM_CONFIGS; cfg++) {
        for (int rep = 0; rep < NUM_REPS; rep++) {
            struct network_trial *trl = &experiment[trial_idx++];
            trl->name = (cfg == 0) ? "PRIORITY, No ECN" :
                        (cfg == 1) ? "PRIORITY + ECN" :
                        (cfg == 2) ? "PRIORITY + ECN + Feedback" :
                        "FIFO + ECN";
            trl->num_packets = num_packets;
            trl->total_rate = total_rate;
            trl->bandwidth_bps = bandwidth_bps;
            trl->propagation_delay_sec = 0.001;
            trl->queue_depth = queue_depth;
            trl->enable_qos = (cfg != 3);
            trl->enable_ecn = (cfg != 0);
            trl->enable_ecn_feedback = (cfg == 2);
            trl->ecn_kmin = 10;
            trl->ecn_kmax = 40;
            trl->rng_seed = cmb_random_hwseed();
        }
    }

    clock_t start = clock();

    if (sequential) {
        printf("Running sequentially...\n");
        for (uint64_t i = 0; i < TOTAL_TRIALS; i++) {
            run_network_trial(&experiment[i]);
        }
    } else {
        printf("Running in parallel using all available CPU cores...\n");
        cimba_run_experiment(experiment, TOTAL_TRIALS, sizeof(*experiment), run_network_trial);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    printf("\n");
    printf("================================================================================\n");
    printf("                           EXPERIMENT RESULTS\n");
    printf("================================================================================\n");
    printf("\n");

    printf("%-30s %12s %10s %10s %10s %10s\n",
           "Configuration", "Delivered", "Loss %", "VoIP ms", "BE ms", "CE Marks");
    printf("%-30s %12s %10s %10s %10s %10s\n",
           "--------------", "----------", "--------", "--------", "--------", "--------");

    for (int cfg = 0; cfg < NUM_CONFIGS; cfg++) {
        uint64_t base = cfg * NUM_REPS;
        double total_delivered = 0, total_sent = 0, total_dropped = 0;
        double total_voip_delay = 0, total_be_delay = 0, total_ce = 0;

        for (int rep = 0; rep < NUM_REPS; rep++) {
            struct network_trial *trl = &experiment[base + rep];
            total_delivered += trl->packets_delivered;
            total_sent += trl->packets_sent;
            total_dropped += trl->packets_dropped;
            total_voip_delay += trl->voip_delay_ms;
            total_be_delay += trl->be_delay_ms;
            total_ce += trl->ce_marked;
        }

        double avg_delivered = total_delivered / NUM_REPS;
        double avg_sent = total_sent / NUM_REPS;
        double loss_pct = (avg_sent > 0) ? (100.0 * (avg_sent - avg_delivered) / avg_sent) : 0;
        double avg_voip = total_voip_delay / NUM_REPS;
        double avg_be = total_be_delay / NUM_REPS;
        double avg_ce = total_ce / NUM_REPS;

        printf("%-30s %12.0f %10.1f %10.1f %10.1f %10.0f\n",
               experiment[base].name, avg_delivered, loss_pct, avg_voip, avg_be, avg_ce);
    }

    printf("\n");
    printf("Total time: %.2f seconds\n", elapsed);
    printf("Trials per second: %.1f\n", TOTAL_TRIALS / elapsed);

    free(experiment);

    printf("\n================================================================================\n");

    return 0;
}