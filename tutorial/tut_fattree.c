/**
 * @file tut_fattree.c
 * @brief Test program for FatTree topology with AllReduce traffic.
 */

#include <cimba.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    //    cmb_logger_flags_off(CMB_LOGGER_INFO);

    const uint64_t seed = cmb_random_hwseed();
    cmb_random_initialize(seed);

    cmb_event_queue_initialize(0.0);

    struct cmb_fattree_config ft_config = {
        .k = 4,
        .link_bandwidth_bps = 1e9,
        .link_propagation_delay_sec = 0.000001,
        .queue_depth = 64,
        .queue_type = CMB_QUEUE_PRIORITY,
        .enable_ecn = true,
        .ecn_kmin = 10,
        .ecn_kmax = 40,
        .enable_red = false,
        .red_min_th = 0,
        .red_max_th = 0,
        .red_max_prob = 0.0,
        .red_q_w = 0.0
    };

    printf("Creating FatTree(k=%u)...\n", ft_config.k);
    struct cmb_fattree *ft = cmb_fattree_create();
    cmb_fattree_initialize(ft, &ft_config);

    printf("FatTree created:\n");
    printf("  Switches: %u\n", cmb_fattree_num_switches(ft));
    printf("  Hosts: %u\n", cmb_fattree_num_hosts(ft));
    printf("  Links: %u\n", cmb_fattree_num_links(ft));

    uint16_t num_hosts = cmb_fattree_num_hosts(ft);

    printf("\nHost addresses:\n");
    for (uint16_t h = 0; h < num_hosts && h < 8; h++) {
        printf("  Host %u: 0x%08X\n", h, cmb_fattree_host_addr(ft, h));
    }
    if (num_hosts > 8) {
        printf("  ... (%u more hosts)\n", num_hosts - 8);
    }

    printf("\nRunning simulation for 0.01 seconds...\n");
    cmb_network_run(ft->net, 0.01);

    printf("\nSimulation complete.\n");
    cmb_fattree_print_stats(ft, stdout);

    cmb_fattree_destroy(ft);

    cmb_event_queue_terminate();
    cmb_random_terminate();

    printf("\nTest complete.\n");
    return 0;
}
