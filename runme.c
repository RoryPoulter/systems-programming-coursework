// Copyright 2025 Rory Poulter

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "./allocator.h"

#define HEAP_MAX 65536  // maximum heap size for testing

int main(int argc, char **argv) {
    // Default test parameters
    unsigned seed = 0;
    unsigned storm = 0;
    size_t heap_size = 2048;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) {
            seed = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--storm") == 0 && i + 1 < argc) {
            storm = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            heap_size = (size_t)atoi(argv[++i]);
            if (heap_size > HEAP_MAX) {
                heap_size = HEAP_MAX;
            }
        }
    }

    // Allocate a static heap buffer
    static uint8_t heap[HEAP_MAX];

    printf("============================Step 1============================\n");
    printf("    mm_init(%hhn, %zu)...\n", heap, heap_size);
    if (mm_init(heap, heap_size) != 0) {
        fprintf(stderr, "    mm_init failed.\n");
        return 1;
    }
    printf("    mm_init passed.\n");

    // for (int i = 0; i < 64; i++) {
    //     printf("%x ", heap[i]);
    // }
    // printf("\n");

    printf("    Heap initialized: size=%zu, seed=%u, storm=%u\n",
        heap_size, seed, storm);
    mm_heap_stats();

    // Simple allocation test
    printf("============================Step 2============================\n");
    printf("    void *p = mm_malloc(128)...\n");
    void *p = mm_malloc(128);
    if (!p) {
        fprintf(stderr, "    mm_malloc failed.\n");

        mm_heap_stats();
        return 1;
    }
    printf("    mm_malloc passed.\n");
    printf("    p mod 40 = %ld\n", (uintptr_t)p % 40);
    mm_heap_stats();

    /**
     * Check repeating pattern is valid. Expected output:
     * c0
     * de
     * f0
     * 0d
     * 55
     */
    printf("============================Step 3============================\n");
    printf("    mm_read(p, 0, pattern_buffer, 5)...\n");
    uint8_t pattern_buffer[5];
    mm_read(p, 0, pattern_buffer, 5);
    for (int j = 0; j < 5; j++) {
        printf("%02x\n", pattern_buffer[j]);
    }

    // Fill memory with some pattern
    printf("============================Step 4============================\n");
    printf("    mm_write(p, 0, data, 128)...\n");
    uint8_t data[128];
    for (int i = 0; i < 128; i++) {
        data[i] = (uint8_t)(i + seed);
    }

    if (mm_write(p, 0, data, 128) != 128) {
        fprintf(stderr, "    mm_write detected corruption\n");
        mm_heap_stats();
        return 1;
    }
    printf("    mm_write passed.\n");
    mm_heap_stats();

    // Read back and verify
    printf("============================Step 5============================\n");
    printf("    mm_read(p, 0, buf, 128)...\n");
    uint8_t buf[128];
    if (mm_read(p, 0, buf, 128) != 128) {
        fprintf(stderr, "    mm_read failed\n");
        return 1;
    }
    printf("    mm_read passed.\n");
    mm_heap_stats();

    for (int i = 0; i < 128; i++) {
        if (buf[i] != data[i]) {
            fprintf(stderr, "    Data mismatch at %d\n", i);
            return 1;
        }
    }

    // Allocating two more blocks.
    printf("============================Step 6============================\n");
    printf("    void *p_2 = mm_malloc(128)...\n");

    void *p_2 = mm_malloc(128);
    if (!p_2) {
        fprintf(stderr, "    mm_malloc failed.\n");
        return 1;
    }
    printf("    p_2 mod 40 = %ld\n", (uintptr_t)p_2 % 40);
    printf("    mm_malloc passed.\n");

    printf("    void *p_3 = mm_malloc(128)...\n");
    void *p_3 = mm_malloc(128);
    if (!p_3) {
        fprintf(stderr, "    mm_malloc failed.\n");
        return 1;
    }
    printf("    p_3 mod 40 = %ld\n", (uintptr_t)p_3 % 40);
    printf("    mm_malloc passed.\n");
    mm_heap_stats();

    // Reallocating the third block.
    printf("============================Step 7============================\n");
    printf("    void *p_4 = mm_realloc(p_3, 256)...\n");
    void *p_4 = mm_realloc(p_3, 256);
    if (!p_4) {
        fprintf(stderr, "    mm_realloc failed.\n");
        return 1;
    }
    printf("    p_4 mod 40 = %ld\n", (uintptr_t)p_4 % 40);
    printf("    mm_realloc passed.\n");
    mm_heap_stats();

    // Free memory
    printf("============================Step 8============================\n");
    printf("    mm_free(p_2)...\n");
    mm_free(p_2);
    mm_heap_stats();

    printf("    mm_free(p)...\n");
    mm_free(p);
    mm_heap_stats();

    printf("    mm_free(p_4)...\n");
    mm_free(p_4);
    mm_heap_stats();
    printf("Basic test passed.\n");

    if (!storm) {
        return 0;
    }

    /**************************************************************************
     *
     * Storm Tests
     *
     *************************************************************************/

    printf("\n\nSimulating storm...\n");

    void *p_5 = mm_malloc(128);
    if (!p_5) {
        fprintf(stderr, "mm_malloc failed.\n");
        return 1;
    }
    printf("Check pointer p_5 = %p is 40-byte aligned: "
        "p mod 40 = %ld\n", p_5, (uintptr_t)p_5 % 40);

    uint8_t data2[128];
    for (int i = 0; i < 128; i++) {
        data2[i] = (uint8_t)(i + seed);
    }

    if (mm_write(p, 0, data2, 128) != 128) {
        fprintf(stderr, "mm_write detected corruption\n");
        mm_heap_stats();
        return 1;
    }
    printf("mm_write passed.\n");

    mm_heap_stats();

    size_t flip_index = seed % heap_size;
    printf("Flip Index: %lu\n", flip_index);
    heap[flip_index] ^= 0xFF;  // simple bit flip
    printf("Simulated bit flip at heap[%zu]\n", flip_index);


    printf("============================Step 9============================\n");
    printf("Reading data written to block 1 with mm_read...\n");
    uint8_t buf2[128];
    if (mm_read(p, 0, buf2, 128) != 128) {
        fprintf(stderr, "mm_read failed\n");
        return 1;
    }
    printf("mm_read passed.\n");
    mm_heap_stats();

    for (int i = 0; i < 128; i++) {
        if (buf2[i] != data2[i]) {
            fprintf(stderr, "Data mismatch at %d\n", i);
            return 1;
        }
    }

    // Attempts to write to quarantined block.
    printf("============================Step 10===========================\n");
    printf("Writing data to block 3 with mm_write...\n");
    uint8_t data_2[128];
    for (int i = 0; i < 128; i++) {
        data_2[i] = (uint8_t)(i + seed);
    }

    if (mm_write(p_5, 0, data_2, 128) != 128) {
        fprintf(stderr, "mm_write detected corruption\n");
        mm_heap_stats();
        return 1;
    }
    printf("mm_write passed.\n");
    mm_heap_stats();



    return 0;
}
