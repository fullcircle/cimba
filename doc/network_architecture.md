/**
 * @file doc_network_architecture.md
 * @brief Architectural Document: Cimba Network Simulation Platform
 *
 * This document describes the internal architecture of the Cimba network
 * simulation components.
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

# Cimba Network Simulation Architecture

## Overview

The Cimba network simulation platform provides discrete event simulation of packet-switched networks with support for:

- Multi-port switches with FIFO or priority queueing
- QoS-aware packet handling
- Explicit Congestion Notification (ECN)
- Tail-drop and blocking overflow behavior
- Comprehensive statistics collection

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Simulation                                   │
│                                                                      │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐      │
│  │ TrafficGen 1  │     │ TrafficGen 2 │     │ TrafficGen N │      │
│  │  (VoIP)      │     │  (BE)        │     │              │      │
│  └──────┬───────┘     └──────┬───────┘     └──────┬───────┘      │
│         │                    │                    │               │
│         └────────────────┬───┴────────────────────┘               │
│                          │                                          │
│                          ▼                                          │
│              ┌───────────────────────┐                             │
│              │    NodeSwitch 1       │                             │
│              │  ┌─────────────────┐  │                             │
│              │  │ Priority Queue  │  │  ┌─────────────┐            │
│              │  │   (per port)    │──┼──│    Link     │            │
│              │  └─────────────────┘  │  └──────┬──────┘            │
│              │  ┌─────────────────┐  │         │                    │
│              │  │  TX Worker(s)  │  │         │                    │
│              │  │  (one per port)│  │         │                    │
│              │  └─────────────────┘  │         │                    │
│              └───────────────────────┘         │                    │
│                                               ▼                    │
│              ┌───────────────────────┐  ┌─────────────┐            │
│              │    NodeSwitch 2       │──│    Link     │            │
│              │  ┌─────────────────┐  │  └─────────────┘            │
│              │  │ Priority Queue  │  │                             │
│              │  │   (per port)    │  │                             │
│              │  └─────────────────┘  │                             │
│              │  ┌─────────────────┐  │                             │
│              │  │  TX Worker(s)  │  │                             │
│              │  └─────────────────┘  │                             │
│              └───────────────────────┘                             │
│                          │                                          │
│                          ▼                                          │
│              ┌───────────────────────┐                              │
│              │    TrafficSink       │                              │
│              │  (receives + stats)  │                              │
│              └───────────────────────┘                              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     Event Queue (Timeline)                           │
│                                                                      │
│  Time:  0.0    0.1    0.2    0.3    0.4    0.5    ...             │
│         ───────●───────●───────●───────●───────●───────▶            │
│                │      │      │      │      │                         │
│           Packet    Timer  Timer  Packet  Wake                        │
│           Arrive   Wake   Exp   Arrive  Up                           │
└─────────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Packet (`cmb_packet`)

The `Packet` is the fundamental data unit in the simulation. Unlike traditional
passive packet structures, Cimba packets **are processes** that actively move
through the network.

```c
struct cmb_packet {
    struct cmb_process proc;           // Embedded process for active movement

    // Packet header
    uint32_t src_addr;                 // Source address
    uint32_t dst_addr;                 // Destination address
    uint32_t size_bits;                // Packet size in bits
    double creation_time;               // Timestamp for delay calculation

    // QoS and marking
    enum cmb_qos_level qos;           // QoS level (0-4)
    enum cmb_ecn_bits ecn;            // ECN codepoint
    bool ecn_marked;                   // Was CE marked during transit

    // VLAN
    uint16_t vlan_id;
    uint8_t vlan_pcp;
};
```

**Key Design Decision**: Making packets `cmb_process` instances allows them to:
- Block and wake up (e.g., waiting in queues)
- Hold timers (e.g., propagation delay on links)
- Be tracked individually in the event system

### 2. NodeSwitch (`cmb_nodeswitch`)

The switch is the central routing component. It connects to links, maintains
routing tables, and manages per-port queues.

```c
struct cmb_nodeswitch {
    struct cmb_process proc;           // Switch processor thread

    char name[64];
    uint32_t local_addr;              // This switch's address
    uint8_t num_ports;
    struct cmb_nodeswitch_port *ports;

    // Routing
    struct cmb_route_entry {
        uint32_t dst_addr;
        uint8_t port;
        struct cmb_link *link;
    } *routing_table;
    uint16_t route_count;

    // Statistics
    struct {
        uint64_t packets_processed;
        uint64_t packets_dropped_route;
        uint64_t packets_dropped_overflow;
    } stats;
};
```

Each port has its own queue:

```c
struct cmb_nodeswitch_port {
    struct cmb_link *link;            // Connected link
    uint8_t peer_port;                // Peer port on link

    // Queue configuration
    enum cmb_queue_type queue_type;
    uint32_t queue_depth;
    struct cmb_objectqueue *queue;     // FIFO: cmb_objectqueue
    struct cmb_priorityqueue *pq;    // PRIORITY: cmb_priorityqueue

    // ECN configuration
    bool enable_ecn;
    uint32_t ecn_kmin;
    uint32_t ecn_kmax;
    uint64_t packets_ecn_marked;

    // TX worker
    struct cmb_process *tx_worker;
    struct cmb_resourceguard *guard;
};
```

### 3. Link (`cmb_link`)

Links connect switches and model physical connections with bandwidth
and propagation delay.

```c
struct cmb_link {
    struct cmb_process proc;

    char name[64];
    struct cmb_nodeswitch *sw1, *sw2;  // Connected switches
    uint8_t port1, port2;              // Connected ports

    // Link characteristics
    uint64_t bandwidth_bits_per_sec;
    double propagation_delay_sec;
    uint64_t buffer_capacity_bits;

    // Transmission state
    struct cmb_process *tx_active;      // Currently transmitting packet
    double tx_start_time;
    double tx_end_time;

    // Statistics
    uint64_t packets_transmitted;
    uint64_t bits_transmitted;
    uint64_t packets_dropped;
};
```

### 4. TrafficGen (`cmb_traffic_gen`)

Traffic generators create packets according to configured patterns.

```c
struct cmb_traffic_gen {
    struct cmb_process proc;
    char name[64];

    struct cmb_nodeswitch *output_switch;
    struct cmb_traffic_gen_config config;

    // Runtime state
    uint64_t packets_sent;
    double next_send_time;
    struct cmb_packet *pkt_pool[16];
    uint8_t pool_count;
};
```

### 5. TrafficSink (`cmb_traffic_sink`)

Traffic sinks receive packets and collect statistics.

```c
struct cmb_traffic_sink {
    struct cmb_process proc;
    char name[64];

    struct cmb_nodeswitch *src_switch;
    uint32_t listen_addr;              // Address to listen on (0 = all)

    bool running;
    struct cmb_traffic_sink_stats stats;
};

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

## Packet Flow

### Transmission Path

```
TrafficGen
    │
    │ Creates packet with:
    │   - dst_addr = destination
    │   - qos = configured level
    │   - ecn = ECT_0 (ECN-capable)
    │
    ▼
cmb_objectqueue_put(switch->input_queue, packet)
    │
    ▼
NodeSwitch::process_packet()
    │
    ├─► Route lookup
    │       │
    │       ▼
    │   cmb_nodeswitch_route_add() installed route
    │   Returns: output port + outgoing link
    │
    ├─► Enqueue to port queue
    │       │
    │       ├─► FIFO: cmb_objectqueue_put()
    │       │
    │       └─► PRIORITY: cmb_priorityqueue_put()
    │               priority = (qos * 8) + vlan_pcp
    │
    ▼
TX Worker (per port)
    │
    ├─► Wait for packet in queue
    │
    ├─► Check ECN marking (if enabled)
    │       │
    │       └─► If queue_len >= kmax:
    │               cmb_packet_mark_ce()
    │
    ├─► Acquire link resource
    │
    ├─► Schedule link transmission
    │       │
    │       └─► cmb_process_timer_add(delay = prop_delay)
    │
    ├─► Link::transmit()
    │       │
    │       └─► On completion:
    │               cmb_objectqueue_put(peer_switch->input_queue)
    │
    ▼
NodeSwitch (next hop)
    │
    └─► ... repeats until dst_addr == local_addr ...
```

### Reception Path (at destination)

```
NodeSwitch (destination)
    │
    └─► Packet dst_addr == local_addr
            │
            ▼
        cmb_objectqueue_put(local_delivery_queue)
            │
            ▼
        TrafficSink
            │
            ├─► Calculate delay = now - packet->creation_time
            │
            ├─► Update aggregate stats
            │
            ├─► Update per-QoS stats
            │       │
            │       └─► Track: packets, bytes, delay, CE marks
            │
            └─► Free packet
```

## Queueing Systems

### FIFO Queue

Uses `cmb_objectqueue` - a simple blocking queue:

- Producers block when full (if `drop_on_overflow=false`)
- Consumers wait when empty
- Packet ordering: FIFO

### Priority Queue

Uses `cmb_priorityqueue` - a blocking priority queue:

- Each packet has a priority value
- `cmb_priorityqueue_get()` returns highest priority packet
- Priority calculation: `(qos * 8) + vlan_pcp`
- Ties broken by arrival order (older first)

### Drop-on-Overflow Behavior

When `port.drop_on_overflow=true`:

```
Enqueue attempt at full queue:
    │
    ├─► FIFO: Return CMB_PROCESS_FULL
    │
    └─► PRIORITY: Return CMB_PROCESS_FULL

Caller (NodeSwitch::process_packet):
    │
    └─► If FULL and drop_on_overflow:
            increment stats.dropped_overflow
            free packet
```

## ECN (Explicit Congestion Notification)

### Overview

ECN allows packets to be marked as experiencing congestion rather than being
dropped. This is modeled after RFC 3168.

### ECN States

```c
enum cmb_ecn_bits {
    CMB_ECN_NOT_ECT = 0,  // Not ECN-capable
    CMB_ECN_ECT_0 = 2,    // ECN-capable, not congested
    CMB_ECN_ECT_1 = 1,    // ECN-capable, alternate codepoint
    CMB_ECN_CE = 3        // Congestion Experienced
};
```

### Marking Logic (TX Worker)

```
On packet dequeue:
    │
    ├─► port.enable_ecn == false:
    │       └──► Don't mark
    │
    └─► port.enable_ecn == true:
            │
            ├─► packet.ecn != ECT_0 and packet.ecn != ECT_1:
            │       └──► Don't mark (not ECN-capable)
            │
            └─► packet already CE-marked:
                    └──► Don't mark (already congested)
            │
            └─► queue_length >= port.ecn_kmax:
                    └──► cmb_packet_mark_ce(pkt)
                         packet.ecn = CE
                         port.packets_ecn_marked++
                         increment qos_stats.ce_marked_count

            └─► queue_length < port.ecn_kmin:
                    └──► Don't mark

            └─► queue_length between kmin and kmax:
                    └──► Probabilistic marking (future)
```

### Key Point

ECN marking happens **at the TX worker** just before transmission, not during
enqueue. This models RED (Random Early Detection) behavior where marking
probability increases with queue occupancy.

## Routing

### Static Routing

Routes are installed manually via `cmb_nodeswitch_route_add()`:

```c
cmb_nodeswitch_route_add(sw,        // Switch
                         0x0A000002, // Destination address
                         0,         // Output port
                         link);     // Output link
```

### Local Delivery

The switch's local address determines which packets are delivered locally:

```c
cmb_nodeswitch_set_local_addr(sw, 0x0A000002);
```

When a packet arrives with `dst_addr == local_addr`, it goes to the
`local_delivery_queue` instead of being forwarded.

### Route Lookup

```c
for (i = 0; i < route_count; i++) {
    if (routing_table[i].dst_addr == packet->dst_addr) {
        return &routing_table[i];
    }
}
return NULL;  // No route - drop packet
```

## Statistics Collection

### Levels

```c
enum cmb_network_stats_level {
    CMB_NETWORK_STATS_NONE = 0,
    CMB_NETWORK_STATS_AGGREGATE = 1,
    CMB_NETWORK_STATS_DETAILED = 2
};
```

### Per-Component Stats

| Component | Metrics |
|-----------|---------|
| Network | Total created, transmitted, dropped, delivered; avg delay |
| NodeSwitch | Processed, dropped (route), dropped (overflow) |
| Port | rx, tx, dropped, queue depth (min/avg/max) |
| Link | Transmitted, bits, dropped |
| TrafficSink | Packets, bytes, delay (min/avg/max), per-QoS breakdown |
| TrafficGen | Packets sent |

### Per-QoS Statistics

The traffic sink tracks statistics per QoS level:

```c
struct cmb_qos_stats {
    uint64_t packets_received;
    uint64_t bytes_received;
    double total_delay_ns;
    double min_delay_ns;
    double max_delay_ns;
    uint64_t ce_marked_count;
};
```

## Process Model

### Overview

Cimba uses a cooperative multitasking model where processes yield by calling
blocking operations. The event queue drives simulation time.

### Key Processes

1. **Event Dispatcher** (`cmb_event_queue_execute`)
   - Central event loop
   - Processes events in time order
   - Wakes blocked processes when events fire

2. **NodeSwitch Processor** (one per switch)
   - Processes packets from input queue
   - Routes to appropriate output port
   - Enqueues to port queues

3. **TX Workers** (one per port)
   - Dequeue from port queue
   - Handle link transmission
   - Manage flow control

4. **TrafficGen** (one per traffic source)
   - Generates packets per configured pattern
   - Schedules next generation

5. **TrafficSink** (one per sink)
   - Receives packets from delivery queue
   - Collects statistics

### Blocking Operations

Processes block by calling:

- `cmb_objectqueue_get()` - wait for item
- `cmb_process_hold()` - wait for duration
- `cmb_resourceguard_wait()` - wait for resource
- `cmb_process_timer_add()` - schedule wakeup

### Resource Guards

TX workers use resource guards to manage access to shared resources:

```c
struct cmb_resourceguard {
    struct cmb_process *owner;        // Current owner
    struct cmb_condition *cond;       // Signaled when released
};
```

The TX worker acquires the guard before transmitting:
```c
cmb_resourceguard_wait(&port->guard);   // Block until acquired
// ... transmit on link ...
cmb_resourceguard_signal(&port->guard); // Release
```

## Memory Management

### Packet Pools

Packets can be allocated from a pool for efficiency:

```c
void cmb_packet_pool_initialize(void);
void cmb_packet_pool_terminate(void);
struct cmb_packet *cmb_packet_create(void);
void cmb_packet_destroy(struct cmb_packet *pp);
```

When the pool is initialized, packets come from a thread-local mempool.
Otherwise, `cmb_process_create()` and `cmi_free()` are used.

### Allocation Strategy

- **Normal path**: `cmb_packet_create()` -> mempool allocation
- **Fallback**: `cmb_process_create()` if pool exhausted
- **Destroy path**: Returns packet to pool or frees directly

## Thread Safety

### Process Model

Each simulation runs in a single thread. The `cimba_run_experiment()`
function parallelizes **across trials**, not within a single trial.

### No Shared Mutable State

Within a trial:
- No writeable global variables
- No static local variables
- Use `CMB_THREAD_LOCAL` if truly needed
- Use normal local variables and function arguments

### Why Single-Threaded?

Discrete event simulation requires deterministic event ordering.
Multi-threaded simulation introduces non-determinism from scheduling
decisions that affect event order.

## Build System

### Using Meson/Ninja

```bash
# Configure
meson setup build

# Compile
ninja -C build

# Install (optional)
ninja -C build install
```

### Manual Compilation

```bash
gcc -I include -I build/include \
    your_app.c \
    -L build/src -lcimba -lm \
    -Wl,-rpath,'$ORIGIN/../src'
```

### Running Tutorials

```bash
# From repository root
cd build
ninja

# Run QoS tutorial
LD_LIBRARY_PATH=build/src \
    ./tutorial/tut_net_qos -n 100 -r 500 -b 1e4 -Q 50 -q 1
```

## Extension Points

### Custom Queue Types

To add a new queue type:
1. Add enum value to `cmb_queue_type`
2. Implement put/get functions
3. Update `cmb_nodeswitch_process_packet()` to handle

### Additional Marking Algorithms

ECN marking is currently threshold-based. To implement RED:
1. Calculate average queue length
2. Compute marking probability from queue occupancy
3. Use `cmb_random_uniform()` to make probabilistic decision

### Traffic Patterns

Traffic generators currently support:
- CBR (constant bit rate)
- Poisson (exponential inter-arrival)
- Bursty (on/off periods)

To add trace-driven traffic:
1. Implement `cmb_traffic_gen_from_trace()`
2. Read packets from file
3. Schedule based on trace timestamps

## Testing

### ECN Marking Tests

The `tut_net_qos.c` tutorial provides comprehensive ECN testing:

```bash
# Compile
gcc -I include -I build/include tutorial/tut_net_qos.c \
    -o tutorial/tut_net_qos build/src/libcimba.so.3.0.0 -lm \
    -Wl,-rpath,'$ORIGIN/../src'

# Test without ECN
LD_PRELOAD=./build/src/libcimba.so.3.0.0 \
    ./tutorial/tut_net_qos -e 0 -n 500 -r 5000 -b 1e4 -Q 100 -q 1

# Test with ECN (early marking)
LD_PRELOAD=./build/src/libcimba.so.3.0.0 \
    ./tutorial/tut_net_qos -e 1 -k 5 -K 20 -n 500 -r 5000 -b 1e4 -Q 100 -q 1

# Test with ECN (late marking)
LD_PRELOAD=./build/src/libcimba.so.3.0.0 \
    ./tutorial/tut_net_qos -e 1 -k 50 -K 90 -n 500 -r 5000 -b 1e4 -Q 100 -q 1
```

### Expected Results

With priority queueing and ECN enabled:
- VoIP packets should have significantly lower delay than BE (priority queueing)
- ECN marks should increase as queue depth exceeds kmin
- More BE packets should be CE-marked than VoIP (BE waits longer in queue)

### TCP-like ECN Feedback

The implementation includes TCP-like ECN feedback:

1. Traffic generators can enable ECN feedback via `enable_ecn_feedback` config
2. Every 20 packets, generators query network for CE-marked packet count
3. If CE rate exceeds threshold (default 10%), congestion factor increases by 1.2×
4. Congestion factor multiplies inter-packet interval (slows sending)
5. When CE rate is low, congestion factor decays by 0.95× per feedback period

### Limitations

Even with ECN feedback implemented, results may show marginal improvement because:

- Offered load (5000 Hz) far exceeds link capacity (~10 packets/sec at 1024 bits)
- Queue fills faster than feedback can throttle senders
- Initial rate has no headroom to back off into

For more visible ECN feedback benefits:
- Use lower initial rate closer to link capacity
- Use more aggressive congestion multiplier (e.g., ×2.0)
- Use faster feedback interval (every 5-10 packets)

## Glossary

| Term | Definition |
|------|------------|
| QoS | Quality of Service - priority levels for traffic handling |
| ECN | Explicit Congestion Notification - congestion signaling without drops |
| ECT | ECN-Capable Transport - packet marked as ECN-capable |
| CE | Congestion Experienced - packet marked indicating congestion |
| FIFO | First-In First-Out - simple queue discipline |
| RED | Random Early Detection - probabilistic drop before queue full |
| TX | Transmit - sending data onto a link |

## Parallel Execution

The `tut_net_qos_parallel.c` example demonstrates using `cimba_run_experiment()` for parallel trials. However, for this event-driven simulation:

### Why Parallel Doesn't Help Here

The Cimba simulation uses **cooperative multitasking** via coroutines. Processes (traffic generators, switches, TX workers, sinks) yield to each other by calling blocking operations. The event dispatcher runs everything in a single thread.

When running multiple trials in parallel:

1. Each trial runs in its own thread
2. Each trial's simulation is event-driven and mostly blocked on queues
3. Thread creation and context switching overhead dominates
4. Result: Parallel is **slower** than sequential for these simulations

### When Parallel Helps

Parallel execution via `cimba_run_experiment()` benefits:

- **Compute-bound trials**: Heavy numerical computations (Monte Carlo, ray tracing)
- **Long-running trials**: Minutes to hours per trial
- **Batch processing**: Many trials submitted to cluster (SLURM, etc.)

### Practical Recommendation

For this network simulation:
- Use sequential execution for development and quick tests
- Use parallel execution only for long-running batch experiments
- Consider using cluster schedulers for very large experiments

## References

- RFC 3168 - The Addition of Explicit Congestion Notification (ECN) to IP
- RFC 2309 - Recommendations on Queue Management and Congestion Avoidance
- Kleinrock, L. "Queueing Systems, Volume I: Theory"