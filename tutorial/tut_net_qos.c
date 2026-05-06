/**
 * @file tut_net_qos.c
 * @brief Tutorial demonstrating QoS-enabled network simulation with congestion scenarios.
 *
 * This tutorial creates a network where VoIP and Best-Effort traffic compete
 * for bandwidth. With priority queueing, VoIP packets get preferential treatment.
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
#include <unistd.h>
#include <getopt.h>

struct traffic_gen_stats {
    uint64_t packets_sent;
};

static void end_sim(void *subject, void *object)
{
    (void)subject;
    (void)object;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -n, --num-pkts <N>       Packets per traffic class (default: 100)\n");
    printf("  -r, --rate <HZ>         Total packet rate in Hz (default: 1000)\n");
    printf("  -b, --bw <BPS>          Link bandwidth in bits/sec (default: 1e5)\n");
    printf("  -d, --delay <SEC>       Propagation delay in sec (default: 0.001)\n");
    printf("  -q, --qos <0|1>         Enable priority queueing (default: 1)\n");
    printf("  -Q, --queue-depth <N>   Queue depth in packets (default: 256)\n");
    printf("  -e, --ecn <0|1>         Enable ECN marking (default: 1)\n");
    printf("  -k, --ecn-kmin <N>      ECN Kmin threshold (default: 10)\n");
    printf("  -K, --ecn-kmax <N>      ECN Kmax threshold (default: 40)\n");
    printf("  -f, --ecn-feedback <0|1> Enable TCP-like ECN feedback (default: 0)\n");
    printf("  -h, --help              Show this help message\n");
    printf("\n");
    printf("Scenario: VoIP (high priority) + Best-Effort (low priority) traffic competing.\n");
    printf("         With QoS enabled, VoIP packets should have lower delay.\n");
    printf("         With -f, senders reduce rate when CE marks are detected (TCP-like).\n");
}

int main(int argc, char *argv[])
{
    uint64_t num_packets = 100;
    double total_rate = 1000;
    double bandwidth_bps = 1e5;
    double propagation_delay_sec = 0.001;
    int enable_qos = 1;
    uint64_t queue_depth = 256;
    int enable_ecn = 1;
    uint32_t ecn_kmin = 10;
    uint32_t ecn_kmax = 40;
    int enable_ecn_feedback = 0;

    static struct option long_options[] = {
        {"num-pkts",  required_argument, 0, 'n'},
        {"rate",      required_argument, 0, 'r'},
        {"bw",        required_argument, 0, 'b'},
        {"delay",     required_argument, 0, 'd'},
        {"qos",       required_argument, 0, 'q'},
        {"queue-depth", required_argument, 0, 'Q'},
        {"ecn",       required_argument, 0, 'e'},
        {"ecn-kmin",  required_argument, 0, 'k'},
        {"ecn-kmax",  required_argument, 0, 'K'},
        {"ecn-feedback", required_argument, 0, 'f'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:r:b:d:q:Q:e:k:K:f:h", long_options, NULL)) != -1) {
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
            case 'd':
                propagation_delay_sec = atof(optarg);
                break;
            case 'q':
                enable_qos = atoi(optarg);
                break;
            case 'Q':
                queue_depth = atoll(optarg);
                break;
            case 'e':
                enable_ecn = atoi(optarg);
                break;
            case 'k':
                ecn_kmin = atoll(optarg);
                break;
            case 'K':
                ecn_kmax = atoll(optarg);
                break;
            case 'f':
                enable_ecn_feedback = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    double voip_rate = total_rate * 0.2;
    double be_rate = total_rate * 0.8;

    printf("\n");
    printf("================================================================================\n");
    printf("               NETWORK SIMULATION - QoS AND CONGESTION DEMO\n");
    printf("================================================================================\n");
    printf("\nNetwork topology:\n");
    printf("  [VoIP + BE Traffic] --> [Switch1] ===Link=== [Switch2] --> [Sink]\n");
    printf("                                      |\n");
    printf("                              (QoS-enabled queue)\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  Link bandwidth:        %.0e bps\n", bandwidth_bps);
    printf("  Packet transmission:  %.2f ms/packet\n", (1024.0 / bandwidth_bps) * 1000);
    printf("  Propagation delay:    %.3f ms\n", propagation_delay_sec * 1000);
    printf("  Queue type:           %s\n", enable_qos ? "PRIORITY" : "FIFO");
    printf("  Queue depth:          %lu packets\n", queue_depth);
    printf("  ECN enabled:          %s\n", enable_ecn ? "YES" : "NO");
    if (enable_ecn) {
        printf("  ECN Kmin/Kmax:       %u/%u\n", ecn_kmin, ecn_kmax);
    }
    printf("\n");
    printf("Traffic Mix (total %.0f Hz = 100%% capacity):\n", total_rate);
    printf("  VoIP (QoS=VOICE):     %.0f Hz (%.0f%%) - HIGH priority\n", voip_rate, (voip_rate/total_rate)*100);
    printf("  BE (QoS=BEST_EFFORT): %.0f Hz (%.0f%%) - LOW priority\n", be_rate, (be_rate/total_rate)*100);
    printf("\n");

    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);

    cmb_event_queue_initialize(0.0);

    struct cmb_network_config net_config = {
        .stats_level = CMB_NETWORK_STATS_AGGREGATE,
        .rng_seed = 0
    };

    struct cmb_network *net = cmb_network_create();
    cmb_network_initialize(net, &net_config);

    enum cmb_queue_type queue_type = enable_qos ? CMB_QUEUE_PRIORITY : CMB_QUEUE_FIFO;

    struct cmb_nodeswitch_port_config port_config = {
        .queue_depth = queue_depth,
        .queue_type = queue_type,
        .port_speed_bits_per_sec = 1e9,
        .drop_on_overflow = true,
        .enable_ecn = enable_ecn ? true : false,
        .ecn_kmin = ecn_kmin,
        .ecn_kmax = ecn_kmax
    };

    struct cmb_nodeswitch *sw1 = cmb_nodeswitch_create();
    cmb_nodeswitch_initialize(sw1, "Switch1", 2, &port_config);

    struct cmb_nodeswitch *sw2 = cmb_nodeswitch_create();
    cmb_nodeswitch_initialize(sw2, "Switch2", 2, &port_config);

    cmb_network_add_switch(net, sw1);
    cmb_network_add_switch(net, sw2);

    struct cmb_link_config link_config = {
        .bandwidth_bits_per_sec = bandwidth_bps,
        .propagation_delay_sec = propagation_delay_sec,
        .buffer_capacity_bits = bandwidth_bps * 10
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
        .num_packets = num_packets,
        .burst_duration_sec = 0.0,
        .burst_scale = 0.0,
        .enable_ecn_feedback = enable_ecn_feedback ? true : false,
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
        .num_packets = num_packets,
        .burst_duration_sec = 0.0,
        .burst_scale = 0.0,
        .enable_ecn_feedback = enable_ecn_feedback ? true : false,
        .ecn_feedback_threshold = 0.1
    };

    struct cmb_traffic_gen *voip_gen = cmb_traffic_gen_create();
    cmb_traffic_gen_initialize(voip_gen, "VoIP-Traffic", sw1, &voip_config, NULL);
    if (enable_ecn_feedback) {
        cmb_traffic_gen_set_network(voip_gen, net);
    }

    struct cmb_traffic_gen *be_gen = cmb_traffic_gen_create();
    cmb_traffic_gen_initialize(be_gen, "BE-Traffic", sw1, &be_config, NULL);
    if (enable_ecn_feedback) {
        cmb_traffic_gen_set_network(be_gen, net);
    }

    struct cmb_traffic_sink *sink = cmb_traffic_sink_create();
    cmb_traffic_sink_initialize(sink, "TrafficSink", sw2, 0x0A000002);
    cmb_network_add_traffic_sink(net, sink);

    printf("Starting simulation...\n");
    cmb_network_start(net);
    cmb_traffic_gen_start(voip_gen);
    cmb_traffic_gen_start(be_gen);
    cmb_traffic_sink_start(sink);

    double sim_duration = (num_packets * 2) / voip_rate + 0.5;
    cmb_event_schedule(end_sim, NULL, NULL, sim_duration, 0);
    cmb_event_queue_execute();

    printf("\n--- Simulation complete ---\n\n");

    cmb_traffic_gen_stop(voip_gen);
    cmb_traffic_gen_stop(be_gen);
    cmb_traffic_sink_stop(sink);
    cmb_network_stop(net);

    uint64_t voip_sent = cmb_traffic_gen_packets_sent(voip_gen);
    uint64_t be_sent = cmb_traffic_gen_packets_sent(be_gen);
    uint64_t total_recv = cmb_traffic_sink_packets_received(sink);

    printf("================================================================================\n");
    printf("                              RESULTS\n");
    printf("================================================================================\n");

    printf("\n--- Traffic Sent ---\n");
    printf("  VoIP (high priority):  %lu packets\n", voip_sent);
    printf("  BE (low priority):     %lu packets\n", be_sent);
    printf("  Total:                 %lu packets\n", voip_sent + be_sent);

    printf("\n--- Traffic Received ---\n");
    printf("  Total received:        %lu packets\n", total_recv);

    if (total_recv > 0) {
        printf("  Avg delay:             %.3f ms\n", cmb_traffic_sink_avg_delay_ns(sink) / 1e6);
        printf("  Min delay:            %.3f ms\n", cmb_traffic_sink_min_delay_ns(sink) / 1e6);
        printf("  Max delay:            %.3f ms\n", cmb_traffic_sink_max_delay_ns(sink) / 1e6);
    }

    printf("\n================================================================================\n");
    printf("                           NETWORK STATISTICS\n");
    printf("================================================================================\n");

    cmb_network_print_stats(net, stdout);

    printf("\n--- Traffic Sink Per-QoS Statistics ---\n");
    cmb_traffic_sink_print_stats(sink, stdout);

    printf("\n================================================================================\n");
    printf("                              SUMMARY\n");
    printf("================================================================================\n");

    uint64_t total_sent = voip_sent + be_sent;
    double loss_rate = (total_sent > 0) ? (100.0 * (total_sent - total_recv) / total_sent) : 0.0;
    double link_util = (total_sent * 1024.0) / (bandwidth_bps * sim_duration) * 100.0;

    printf("\n  Total sent:        %lu packets\n", total_sent);
    printf("  Total received:   %lu packets\n", total_recv);
    printf("  Loss rate:         %.2f%%\n", loss_rate);
    printf("  Link utilization:  %.1f%%\n", link_util > 100 ? 100 : link_util);
    printf("\n");

    if (enable_qos) {
        printf("Result: With PRIORITY queueing, VoIP packets (higher QoS) are dequeued\n");
        printf("        before BE packets when the link is congested.\n");
    } else {
        printf("Result: With FIFO queueing, packets are served in order of arrival.\n");
        printf("        Both traffic classes get similar service regardless of QoS.\n");
    }

    cmb_traffic_gen_destroy(voip_gen);
    cmb_traffic_gen_destroy(be_gen);
    cmb_traffic_sink_destroy(sink);
    cmb_network_destroy(net);

    cmb_event_queue_terminate();
    cmb_random_terminate();

    printf("\n================================================================================\n");
    printf("                              DEMO COMPLETE\n");
    printf("================================================================================\n");

    return 0;
}