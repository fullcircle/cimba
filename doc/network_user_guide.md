/**
 * @file doc_network_user_guide.md
 * @brief User Guide: Network Simulation with Cimba
 *
 * This guide explains how to use the Cimba discrete event simulation library
 * to build network simulations with QoS, ECN, and priority queueing support.
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

# Cimba Network Simulation User Guide

## Overview

Cimba provides a discrete event simulation framework for building network simulations.
This guide covers the network simulation components:

- **Network Container** - Holds all simulation components
- **NodeSwitch** - Network switch/router with ports and queues
- **Link** - Point-to-point link between switches
- **TrafficGen** - Generates packet traffic
- **TrafficSink** - Receives packets and collects statistics
- **Packet** - Individual data packets with QoS/ECN markings

## Quick Start

```c
#include <cimba.h>
#include <stdio.h>

int main() {
    // Initialize random number generator
    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);

    // Initialize event queue
    cmb_event_queue_initialize(0.0);

    // Create network
    struct cmb_network *net = cmb_network_create();
    cmb_network_initialize(net, &(struct cmb_network_config){
        .stats_level = CMB_NETWORK_STATS_AGGREGATE,
        .rng_seed = 0
    });

    // Create switch with priority queueing
    struct cmb_nodeswitch_port_config port_config = {
        .queue_depth = 256,
        .queue_type = CMB_QUEUE_PRIORITY,
        .port_speed_bits_per_sec = 1e9,
        .drop_on_overflow = true
    };

    struct cmb_nodeswitch *sw = cmb_nodeswitch_create();
    cmb_nodeswitch_initialize(sw, "Switch1", 2, &port_config);
    cmb_network_add_switch(net, sw);

    // Create link
    struct cmb_link *link = cmb_link_create();
    cmb_link_initialize(link, "Link1-2", sw1, 0, sw2, 0, &(struct cmb_link_config){
        .bandwidth_bits_per_sec = 1e6,
        .propagation_delay_sec = 0.01,
        .buffer_capacity_bits = 1e6
    });
    cmb_network_add_link(net, link);

    // Add routing
    cmb_nodeswitch_route_add(sw1, 0x0A000002, 0, link);
    cmb_nodeswitch_set_local_addr(sw2, 0x0A000002);

    // Create traffic generator
    struct cmb_traffic_gen_config gen_config = {
        .src_addr = 0x0A000001,
        .dst_addr = 0x0A000002,
        .packet_rate_hz = 100,
        .packet_size_bits = 1024,
        .pattern = CMB_TRAFFIC_POISSON,
        .qos = CMB_QOS_VIDEO,
        .num_packets = 100
    };

    struct cmb_traffic_gen *gen = cmb_traffic_gen_create();
    cmb_traffic_gen_initialize(gen, "Traffic", sw1, &gen_config, NULL);

    // Create sink
    struct cmb_traffic_sink *sink = cmb_traffic_sink_create();
    cmb_traffic_sink_initialize(sink, "Sink", sw2, 0x0A000002);

    // Run simulation
    cmb_network_start(net);
    cmb_traffic_gen_start(gen);
    cmb_traffic_sink_start(sink);
    cmb_event_queue_execute();

    // Print results
    cmb_traffic_sink_print_stats(sink, stdout);
    cmb_network_print_stats(net, stdout);

    // Cleanup
    cmb_traffic_gen_destroy(gen);
    cmb_traffic_sink_destroy(sink);
    cmb_network_destroy(net);
    cmb_event_queue_terminate();
    cmb_random_terminate();

    return 0;
}
```

## Key Concepts

### 1. Initialization Sequence

Always initialize components in this order:

```c
// 1. Random number generator
cmb_random_initialize(seed);

// 2. Event queue
cmb_event_queue_initialize(0.0);

// 3. Network
struct cmb_network *net = cmb_network_create();
cmb_network_initialize(net, &config);

// 4. Switches (before links, as links reference switches)
struct cmb_nodeswitch *sw = cmb_nodeswitch_create();
cmb_nodeswitch_initialize(sw, "Switch", num_ports, &port_config);
cmb_network_add_switch(net, sw);

// 5. Links
struct cmb_link *link = cmb_link_create();
cmb_link_initialize(link, "Link", sw1, port1, sw2, port2, &link_config);
cmb_network_add_link(net, link);

// 6. Connect ports
cmb_nodeswitch_set_port_link(sw1, port, link, peer_port);

// 7. Add routes
cmb_nodeswitch_route_add(sw, destination_addr, output_port, link);
cmb_nodeswitch_set_local_addr(sw, local_addr);

// 8. Traffic generators and sinks
struct cmb_traffic_gen *gen = cmb_traffic_gen_create();
cmb_traffic_gen_initialize(gen, name, switch, &config, NULL);
struct cmb_traffic_sink *sink = cmb_traffic_sink_create();
cmb_traffic_sink_initialize(sink, name, switch, listen_addr);
```

### 2. Address Format

Addresses are 32-bit integers, typically written as hex:
- `0x0A000001` = 10.0.0.1
- `0x0A000002` = 10.0.0.2

### 3. QoS Levels

Packets have a QoS level that affects priority queueing:

```c
enum cmb_qos_level {
    CMB_QOS_BEST_EFFORT = 0,    // Lowest priority
    CMB_QOS_PRIORITY = 1,
    CMB_QOS_VIDEO = 2,
    CMB_QOS_VOICE = 3,          // Highest priority among standard levels
    CMB_QOS_CONTROL = 4
};
```

Priority is calculated as: `(qos * 8) + vlan_pcp`
Higher values are dequeued first in priority queueing mode.

### 4. ECN (Explicit Congestion Notification)

ECN allows packets to be marked rather than dropped when congestion occurs:

```c
enum cmb_ecn_bits {
    CMB_ECN_NOT_ECT = 0,  // Not ECN-capable transport
    CMB_ECN_ECT_0 = 2,    // ECN-capable, not experiencing congestion
    CMB_ECN_ECT_1 = 1,    // ECN-capable, alternative codepoint
    CMB_ECN_CE = 3       // Congestion Experienced
};
```

Enable ECN on ports:
```c
struct cmb_nodeswitch_port_config port_config = {
    .queue_depth = 256,
    .queue_type = CMB_QUEUE_PRIORITY,
    .enable_ecn = true,
    .ecn_kmin = 10,       // Mark when queue >= 10
    .ecn_kmax = 50        // Always mark when queue >= 50
};
```

### 5. Queue Types

```c
enum cmb_queue_type {
    CMB_QUEUE_FIFO = 0,      // First-in, first-out
    CMB_QUEUE_PRIORITY = 1   // Priority-based dequeue
};
```

### 6. Traffic Patterns

```c
enum cmb_traffic_pattern {
    CMB_TRAFFIC_CBR,         // Constant bit rate
    CMB_TRAFFIC_POISSON,     // Random arrivals (exponential inter-arrival)
    CMB_TRAFFIC_BURSTY,      // Bursty traffic with on/off periods
    CMB_TRAFFIC_TRACE         // From trace file (future)
};
```

## Configuration Reference

### cmb_nodeswitch_port_config

```c
struct cmb_nodeswitch_port_config {
    uint32_t queue_depth;              // Max packets in queue
    enum cmb_queue_type queue_type;    // FIFO or PRIORITY
    uint64_t port_speed_bits_per_sec;  // Port line rate
    bool drop_on_overflow;             // Drop vs block on full queue
    bool enable_ecn;                   // Enable ECN marking
    uint32_t ecn_kmin;                 // ECN marking threshold (lower)
    uint32_t ecn_kmax;                 // ECN marking threshold (upper)
};
```

### cmb_traffic_gen_config

```c
struct cmb_traffic_gen_config {
    uint32_t src_addr;                 // Source address
    uint32_t dst_addr;                 // Destination address
    double packet_rate_hz;             // Generation rate in Hz
    uint32_t packet_size_bits;         // Packet size in bits
    enum cmb_traffic_pattern pattern; // Traffic pattern
    enum cmb_qos_level qos;           // QoS level for packets
    uint16_t vlan_id;                  // VLAN ID (0 = none)
    uint8_t vlan_pcp;                  // VLAN Priority Code Point
    uint32_t num_packets;              // Packets to generate (0 = unlimited)
    double burst_duration_sec;         // Burst on-duration
    double burst_scale;                // Burst scale factor
};
```

### cmb_link_config

```c
struct cmb_link_config {
    uint64_t bandwidth_bits_per_sec;    // Link speed
    double propagation_delay_sec;       // Propagation latency
    uint64_t buffer_capacity_bits;      // Buffer size
};
```

## Statistics

### Traffic Sink Statistics

```c
struct cmb_traffic_sink_stats {
    uint64_t packets_received;
    uint64_t bytes_received;
    double total_delay_ns;
    double min_delay_ns;
    double max_delay_ns;
    struct cmb_datasummary delay_summary;
    struct cmb_qos_stats qos_stats[5];  // Per-QoS breakdown
};
```

### Network Statistics

The network collects:
- Total packets created, transmitted, dropped, delivered
- Average delay across all packets
- Per-switch packet counts and queue depths
- Per-link transmission statistics

### Accessing Statistics

```c
// From traffic sink
uint64_t pkts = cmb_traffic_sink_packets_received(sink);
double avg_delay = cmb_traffic_sink_avg_delay_ns(sink);

// From network
uint64_t transmitted = cmb_network_packets_transmitted(net);
uint64_t dropped = cmb_network_packets_dropped(net);
double avg_delay = cmb_network_avg_delay_ns(net);

// Print to file
cmb_traffic_sink_print_stats(sink, stdout);
cmb_network_print_stats(net, stdout);
```

## Example: Two-Tier Network with Competing Traffic

```c
// Create network with VoIP + Best-Effort competing
struct cmb_nodeswitch *sw1 = cmb_nodeswitch_create();
cmb_nodeswitch_initialize(sw1, "AccessSwitch", 2, &port_config);

struct cmb_nodeswitch *sw2 = cmb_nodeswitch_create();
cmb_nodeswitch_initialize(sw2, "CoreSwitch", 2, &port_config);

cmb_network_add_switch(net, sw1);
cmb_network_add_switch(net, sw2);

// Connect with congested link
struct cmb_link *uplink = cmb_link_create();
cmb_link_initialize(uplink, "Uplink", sw1, 0, sw2, 0, &link_config);
cmb_network_add_link(net, uplink);

cmb_nodeswitch_set_port_link(sw1, 0, uplink, 0);
cmb_nodeswitch_set_port_link(sw2, 0, uplink, 0);

// VoIP traffic (high priority)
struct cmb_traffic_gen_config voip_config = {
    .src_addr = 0x0A000001,
    .dst_addr = 0x0A000002,
    .packet_rate_hz = 100,
    .packet_size_bits = 256,
    .pattern = CMB_TRAFFIC_CBR,
    .qos = CMB_QOS_VOICE,
    .num_packets = 1000
};

// Best-Effort traffic (low priority)
struct cmb_traffic_gen_config be_config = {
    .src_addr = 0x0A000001,
    .dst_addr = 0x0A000002,
    .packet_rate_hz = 900,
    .packet_size_bits = 1024,
    .pattern = CMB_TRAFFIC_POISSON,
    .qos = CMB_QOS_BEST_EFFORT,
    .num_packets = 1000
};
```

## Building and Running

### Compilation

```bash
# Using the build system (recommended)
cd build
ninja

# Manual compilation
gcc -I include -I build/include your_app.c \
    -L build/src -lcimba -lm \
    -Wl,-rpath,'$ORIGIN/../src'
```

### Running

```bash
# Set library path
export LD_LIBRARY_PATH=build/src:$LD_LIBRARY_PATH

# Run tutorial example
./tutorial/tut_net_qos -n 100 -r 500 -b 1e4 -Q 50 -q 1

# Options:
#   -n, --num-pkts      Packets per traffic class
#   -r, --rate          Total packet rate in Hz
#   -b, --bw            Link bandwidth in bits/sec
#   -d, --delay         Propagation delay in sec
#   -q, --qos           Enable priority queueing (0=FIFO, 1=PRIORITY)
#   -Q, --queue-depth   Queue depth in packets
```

## Best Practices

1. **Always check return values** - Many functions return bool to indicate success/failure

2. **Initialize in correct order** - Network components have dependencies:
   - Switches must exist before links reference them
   - Routes must be added after ports are connected

3. **Set local addresses** - Traffic sinks need `cmb_nodeswitch_set_local_addr()` to receive packets

4. **Match rates to capacity** - Oversubscribed links cause drops; use ECN for congestion signaling

5. **Use appropriate queue depths** - Deep queues increase latency; shallow queues increase drops

6. **Priority queueing requires ECN disabled on senders** - If traffic generators don't mark ECT, switches can't mark CE

7. **Clean up in reverse order** - Destroy what you create in reverse order of creation

## Troubleshooting

### Packets not reaching destination
- Verify routes: `cmb_nodeswitch_route_add()`
- Set local address: `cmb_nodeswitch_set_local_addr()`
- Check destination address in traffic_gen matches sink's listen_addr

### All packets dropped
- Check link bandwidth vs packet rate
- Verify queue_depth is sufficient
- Enable `drop_on_overflow` if blocking behavior is causing deadlock

### No priority effect seen
- Ensure `queue_type = CMB_QUEUE_PRIORITY`
- Verify traffic generators set different QoS levels
- Check QoS values: VOICE (3) > BE (0) should give VoIP priority

### ECN not marking packets
- Enable ECN: `port.enable_ecn = true`
- Set thresholds: `port.ecn_kmin`, `port.ecn_kmax`
- Ensure packets have ECT set: `cmb_packet_set_ect(pkt, CMB_ECN_ECT_0)`

## ECN Testing

The `tut_net_qos.c` tutorial supports ECN testing via command-line flags:

```bash
# Run without ECN
./tutorial/tut_net_qos -e 0 -n 500 -r 5000 -b 1e4 -Q 100 -q 1

# Run with ECN enabled (marking only)
./tutorial/tut_net_qos -e 1 -k 10 -K 80 -n 500 -r 5000 -b 1e4 -Q 100 -q 1

# Run with ECN enabled and TCP-like feedback
./tutorial/tut_net_qos -e 1 -f 1 -n 500 -r 5000 -b 1e4 -Q 100 -q 1
```

### Example Test Results

Test parameters: 500 packets/class, 5000 Hz total rate, 10^4 bps link, 100 queue depth

| Configuration | Delivered | VoIP Delay | BE Delay | CE Marked |
|--------------|-----------|------------|----------|------------|
| PRIORITY, No ECN | 118 | 359ms | 5499ms | 0 |
| PRIORITY + ECN (marking only) | 119 | 558ms | 5174ms | 79 |
| PRIORITY + ECN (with feedback) | 121 | 532ms | 5328ms | 78 |

### Key Findings

1. **Priority queueing is effective** - VoIP packets get ~15x lower delay than BE

2. **ECN marking works** - Packets are marked CE when queue depth exceeds thresholds

3. **ECN feedback provides marginal improvement** - With TCP-like feedback, slightly more packets delivered and lower delay. However, when offered load far exceeds link capacity, feedback cannot prevent congestion.

4. **BE traffic gets more CE marks** - Because BE is queued behind VoIP in priority mode, it experiences more congestion and marking

### ECN Threshold Effects

| Threshold | CE Marks | Effect |
|-----------|----------|--------|
| kmin=5, kmax=20 (early) | 99 | Many packets marked, senders notified early |
| kmin=10, kmax=80 (default) | 40-80 | Moderate marking |
| kmin=50, kmax=90 (late) | 28 | Few packets marked, late notification |

Lower thresholds cause more aggressive marking, giving senders earlier notice of congestion.

## Parallel Execution

The Cimba framework supports running multiple simulation trials in parallel using `cimba_run_experiment()`. This is useful when you need to:

- Run multiple configurations (e.g., QoS on/off, ECN on/off)
- Run multiple replications for statistical confidence
- Utilize multiple CPU cores

### Example: Parallel Network Experiment

See `tutorial/tut_net_qos_parallel.c` for a complete example. The key concepts:

```c
// Define a trial struct with parameters and results
struct network_trial {
    uint64_t num_packets;
    double total_rate;
    double bandwidth_bps;
    int enable_qos;
    int enable_ecn;
    // ... results fields
    uint64_t packets_delivered;
    double avg_delay_ns;
};

// Trial function - runs one complete simulation
void run_network_trial(void *vtrl) {
    struct network_trial *trl = vtrl;

    // Initialize simulation
    cmb_event_queue_initialize(0.0);
    cmb_random_initialize(trl->rng_seed);

    // Create network, run simulation, collect results
    // ...

    // Store results back to trial struct
    trl->packets_delivered = cmb_network_packets_delivered(net);

    // Cleanup
    cmb_event_queue_terminate();
}

// Run trials in parallel
cimba_run_experiment(experiment, n_trials, sizeof(*experiment), run_network_trial);
```

### Running Parallel Simulations

```bash
# Compile
gcc -I include -I build/include tutorial/tut_net_qos_parallel.c \
    -o tutorial/tut_net_qos_parallel build/src/libcimba.so.3.0.0 -lm \
    -Wl,-rpath,'$ORIGIN/../src'

# Run using all available CPU cores
LD_PRELOAD=./build/src/libcimba.so.3.0.0 ./tutorial/tut_net_qos_parallel

# Run sequentially (for comparison)
LD_PRELOAD=./build/src/libcimba.so.3.0.0 ./tutorial/tut_net_qos_parallel -s
```

### Performance Considerations

For this event-driven network simulation, parallel execution via `cimba_run_experiment()` provides **marginal benefit**:

- **Event-driven design**: Simulations spend most time blocked on queues, not doing CPU work
- **Coroutine model**: Multiple processes within a simulation yield cooperatively
- **Thread overhead**: Creating and managing threads adds overhead

**Observed results on 32-core system (100 trials):**

| Execution Mode | Time | Trials/sec |
|---------------|------|----------|
| Sequential | 60.5s | 8.3 |
| Parallel | 61.1s | 8.2 |

Even with 100 trials, parallel provides no speedup. This is because the simulation is **I/O-bound** (blocked on event queues), not CPU-bound.

**When parallel execution is beneficial:**

- **CPU-intensive trials**: Trials doing heavy computation (Monte Carlo, ray tracing)
- **Long-running trials**: Minutes per trial, where thread overhead is amortized
- **Batch jobs**: Many trials submitted to a cluster scheduler (SLURM, PBS, etc.)

**For this network simulation:** Sequential execution is recommended. The event-driven nature means threads spend most time blocked, not computing.

## See Also

- `tutorial/tut_net_qos.c` - Complete example with QoS
- `tutorial/tut_net_qos_parallel.c` - Parallel experiment framework
- `tutorial/tut_net_sim.c` - Simple end-to-end example
- `include/cmb_network.h` - Network API reference
- `include/cmb_nodeswitch.h` - Switch API reference
- `include/cmb_packet.h` - Packet and QoS definitions