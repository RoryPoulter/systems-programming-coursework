// Copyright 2025 Rory Poulter

/******************************************************************************
 *
 * Includes and Definitions
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "./allocator.h"

#define ALIGN 40
#define ALIGN_UP(x) (((x)+(ALIGN-1))/ALIGN*ALIGN)

// Patterns for unused space
static uint8_t UNUSED_PATTERN[5] = {0xC0, 0xDE, 0xF0, 0x0D, 0x55};

// Metadata constants
#define GLOBAL_HEADER_CONSISTENCY 0xDEADCE11
#define BLOCK_HEADER_CONSISTENCY 0xC0DEBA5E
#define BLOCK_FOOTER_CONSISTENCY 0xBAD1DEA5

// Block states
#define FREE 0
#define USED 1
#define QUARANTINED 2


/******************************************************************************
 *
 * Data Structures
 *
 *****************************************************************************/


// ----- GLOBAL HEADER -----
typedef struct {
    uint32_t consistency;
    uint32_t heap_size;
    uint32_t first_block;
    uint32_t crc;
} GlobalHeader;

// ----- BLOCK HEADER -----
typedef struct {
    uint32_t consistency;
    uint32_t size;
    uint32_t state;
    uint32_t prev;
    uint32_t next;
    uint32_t seq;
    uint32_t crc;
    uint8_t  pad_size;      // NEW: padding after footer
    uint8_t  _pad[11];      // keep structure at 40 bytes
} BlockHeader;

// ----- BLOCK FOOTER -----
typedef struct {
    uint32_t consistency;
    uint32_t size;
    uint32_t seq;
    uint32_t crc;
    uint8_t  pad_size;      // NEW: padding mirrored
    uint8_t  _pad[3];
} BlockFooter;

// Globals
static uint8_t *g_heap = NULL;
static size_t g_heap_size = 0;

// 1 is debug information should be displayed, 0 if not.
static int debug = 0;


/******************************************************************************
 *
 * Checksum Functions
 *
 *****************************************************************************/


/**
 * @brief Calculates the CRC32 value.
 * 
 * @param data The pointer to the data to calculate the CRC32 checksum
 * value for.
 * @param len The length of the data.
 * @return uint32_t The CRC32 checksum value.
 */
uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
    }
    return crc ^ 0xFFFFFFFFu;
}


/**
 * @brief Calculate the CRC32 checksum using the metadata in a block header
 * excluding the CRC32 value.
 * 
 * @param h A pointer to the block header.
 * @return uint32_t The CRC32 checksum.
 */
uint32_t get_header_crc(const BlockHeader *h) {
    return crc32(h, offsetof(BlockHeader, crc));
}


/**
 * @brief Calculate the CRC32 checksum using the metadata in a block footer
 * excluding the CRC32 value.
 * 
 * @param h A pointer to the block footer.
 * @return uint32_t The CRC32 checksum.
 */
uint32_t get_footer_crc(const BlockFooter *f) {
    return crc32(f, offsetof(BlockFooter, crc));
}


/******************************************************************************
 *
 * Helper Functions
 *
 *****************************************************************************/


/**
 * @brief Generates an offset corresponding to a pointer.
 * 
 * @param p The pointer.
 * @return uint32_t The offset corresponding to the pointer.
 */
static inline uint32_t ptr_to_off(void *p) {
    if (!p) return 0;
    return (uint32_t)((uint8_t*)p - g_heap);
}


/**
 * @brief Generates a pointer to data located `offset` bytes away.
 * 
 * @param offset The offset of the pointer to be generated.
 * @return void* The pointer corresponding to the offset.
 */
static inline void *off_to_ptr(uint32_t off) {
    if (off == 0 || off >= g_heap_size) return NULL;
    return g_heap + off;
}


/**
 * @brief Get a pointer to the footer from a pointer of the header and the 
 * block size.
 * 
 * @param h The header pointer.
 * @return * BlockFooter* A pointer to the footer.
 */
static inline BlockFooter *get_footer(BlockHeader *h) {
    return (BlockFooter*)((uint8_t*)h + sizeof(BlockHeader) + h->size);
}


/**
 * @brief Computes a pointer to the next block.
 * 
 * @param h A pointer to the current block.
 * @return uint8_t* A pointer to the next block.
 */
static inline uint8_t *block_next_header(BlockHeader *h) {
    BlockFooter *f = get_footer(h);
    uintptr_t addr = (uintptr_t)f + sizeof(BlockFooter) + f->pad_size;
    return (uint8_t*)addr;
}


/**
 * @brief Checks the block metadata for validity.
 * 
 * @param h The pointer to the block header.
 * @return * int A boolean indicating if the block is valid.
 */
int is_block_valid(BlockHeader *h) {
    // Input sanitisation
    if (debug != 1 && debug != 0) {
        debug = 0;
    }
    if (!h) {
        if (debug) printf("No header passed.\n");
        return 0;
    }

    // Check if header pointer is within the heap bounds.
    if ((uint8_t*)h < g_heap || (uint8_t*)h + sizeof(BlockHeader) >
    g_heap + g_heap_size) {
        if (debug) printf("Header is out of heap bounds.\n");
        return 0;
    }

    // Check the header metadata constant.
    if (h->consistency != BLOCK_HEADER_CONSISTENCY) {
        if (debug) printf("Header metadata constant is invalid.\n");
        return 0;
    }

    // Check the block size does not exceed the heap size.
    if (h->size > g_heap_size) {
        if (debug) printf("Block size exceeds heap size.\n");
        return 0;
    }

    // Check the block header checksum is consistent.
    if (h->crc != get_header_crc(h)) {
        if (debug) printf("Header checksum not valid.\n");
        return 0;
    }

    // Retrieve the footer pointer from the header.
    BlockFooter *f = get_footer(h);

    // Check if the footer is beyond the heap bounds.
    if ((uint8_t*)f + sizeof(BlockFooter) > g_heap + g_heap_size) {
        if (debug) printf("Footer is out of heap bounds.\n");
        return 0;
    }

    // Check the footer metadata constant.
    if (f->consistency != BLOCK_FOOTER_CONSISTENCY) {
        if (debug) printf("Footer metadata constant is invalid.\n");
        return 0;
    }

    // Check the footer size equals the header size.
    if (f->size != h->size) {
        if (debug) printf("Footer size does not match header.\n");
        return 0;
    }

    // Check the footer sequence number equals the header sequence number.
    if (f->seq != h->seq) {
        if (debug) printf("Footer sequence does not match header.\n");
        return 0;
    }

    // Check the block footer checksum is consistent.
    if (f->crc != get_footer_crc(f)) {
        if (debug) printf("Footer checksum not valid.\n");
        return 0;
    }

    // If all the checks have passed, the block is valid.
    if (debug) printf("All checks passed.\n");
    return 1;
}


/**
 * @brief Updates the metadata of a block when it has been corrupted so the
 * memory allocator will quarantine it.
 * 
 * @param h A pointer to the header of the corrupted block.
 */
void quarantine_block(BlockHeader *h) {
    if (!h) return;
    BlockFooter *f = get_footer(h);

    h->state = QUARANTINED;
    h->seq++;

    f->seq = h->seq;
    f->size = h->size;
    f->pad_size = h->pad_size;

    f->crc = get_footer_crc(f);
    h->crc = get_header_crc(h);
}


/**
 * @brief Computes the padding after the footer to align the next block.
 * 
 * @param h A pointer to the header of the current block.
 * @return uint8_t The padding required to align the next payload.
 */
static inline uint8_t compute_padding(BlockHeader *h) {
    uintptr_t footer_end =
        (uintptr_t)h +
        sizeof(BlockHeader) +
        h->size +
        sizeof(BlockFooter);

    /* The NEXT HEADER must be aligned in real memory */
    uint8_t pad = (ALIGN - (footer_end % ALIGN)) % ALIGN;
    return pad;
}


/**
 * @brief Splits a block into two parts and sets the new partition to `FREE`
 * 
 * @param h A pointer to the block to split
 * @param needed The size of the block to be split
 */
void split_block(BlockHeader *h, size_t needed) {
    if (!h) return;
    if (h->size < needed) return;

    size_t old_size = h->size;

    // shrink original
    h->size = needed;
    h->seq++;

    BlockFooter *f1 = get_footer(h);
    f1->consistency = BLOCK_FOOTER_CONSISTENCY;
    f1->size = h->size;
    f1->seq = h->seq;

    /* NEW: compute padding based on absolute pointer alignment */
    h->pad_size = compute_padding(h);
    f1->pad_size = h->pad_size;

    /* CRCs */
    f1->crc = get_footer_crc(f1);
    h->crc = get_header_crc(h);

    /* Now place the next header */
    uint8_t *new_hdr_addr = block_next_header(h);

    uint8_t *block_end = (uint8_t*)h
        + sizeof(BlockHeader)
        + old_size
        + sizeof(BlockFooter);

    if (new_hdr_addr + sizeof(BlockHeader) + sizeof(BlockFooter) > block_end)
        return;

    BlockHeader *nh = (BlockHeader*)new_hdr_addr;

    nh->consistency = BLOCK_HEADER_CONSISTENCY;
    nh->state = FREE;
    nh->size = (size_t)(block_end - (new_hdr_addr + sizeof(BlockHeader) +
    sizeof(BlockFooter)));
    nh->prev = ptr_to_off(h);
    nh->next = h->next;
    nh->seq = 1;

    nh->pad_size = compute_padding(nh);

    nh->crc = get_header_crc(nh);

    BlockFooter *f2 = get_footer(nh);
    f2->consistency = BLOCK_FOOTER_CONSISTENCY;
    f2->size = nh->size;
    f2->seq = nh->seq;
    f2->pad_size = nh->pad_size;
    f2->crc = get_footer_crc(f2);

    if (h->next) {
        BlockHeader *nx = off_to_ptr(h->next);
        if (is_block_valid(nx)) {
            nx->prev = ptr_to_off(nh);
            nx->crc = get_header_crc(nx);

            BlockFooter *nxf = get_footer(nx);
            nxf->seq = nx->seq;
            nxf->pad_size = nx->pad_size;
            nxf->crc = get_footer_crc(nxf);
        } else {
            quarantine_block(nx);
        }
    }

    h->next = ptr_to_off(nh);
    h->crc = get_header_crc(h);
}


/**
 * @brief Merges adjacent free valid blocks into one contiguous block.
 * 
 * @param h A pointer to the newly freed block.
 */
void merge_free_blocks(BlockHeader *h) {
    if (!h) return;

    while (h->next) {
        BlockHeader *h2 = off_to_ptr(h->next);
        if (!h2 || !is_block_valid(h2) || h2->state != FREE)
            break;

        /* STRICT absolute-pointer adjacency */
        uint8_t *expected = block_next_header(h);
        if ((uint8_t*)h2 != expected)
            break;

        BlockFooter *f2 = get_footer(h2);
        uint8_t *end_h2 =
            (uint8_t*)f2 + sizeof(BlockFooter) + f2->pad_size;

        /* Merge sizes */
        uint8_t *payload_start = (uint8_t*)h + sizeof(BlockHeader);
        size_t new_size =
            (size_t)(end_h2 - payload_start - sizeof(BlockFooter));

        h->size = new_size;
        h->seq++;

        /* Relink */
        h->next = h2->next;
        if (h2->next) {
            BlockHeader *h3 = off_to_ptr(h2->next);
            if (is_block_valid(h3)) {
                h3->prev = ptr_to_off(h);
                h3->crc = get_header_crc(h3);
                BlockFooter *f3 = get_footer(h3);
                f3->seq = h3->seq;
                f3->pad_size = h3->pad_size;
                f3->crc = get_footer_crc(f3);
            }
        }

        /* Recompute padding and update footer */
        BlockFooter *f = get_footer(h);
        f->consistency = BLOCK_FOOTER_CONSISTENCY;
        f->size = h->size;
        f->seq = h->seq;

        h->pad_size = compute_padding(h);
        f->pad_size = h->pad_size;

        f->crc = get_footer_crc(f);
        h->crc = get_header_crc(h);
    }
}


/**
 * @brief Finds the block which a pointer falls within the bounds of.
 * 
 * @param ptr The pointer to identify the block for.
 * @return BlockHeader* A pointer to the header of the block if found, or NULL.
 */
BlockHeader *ptr_to_block(void *ptr) {
    if (!ptr) return NULL;

    uint8_t *p = ptr;
    if (p < g_heap || p >= g_heap + g_heap_size) return NULL;

    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t off = G->first_block;

    while (off) {
        BlockHeader *h = off_to_ptr(off);
        if (!h) return NULL;

        if (!is_block_valid(h)) {
            quarantine_block(h);
            off = h->next;
            continue;
        }

        uint8_t *start = (uint8_t*)h + sizeof(BlockHeader);
        uint8_t *end = start + h->size;

        if (p >= start && p < end && h->state == USED)
            return h;

        off = h->next;
    }

    return NULL;
}


/******************************************************************************
 *
 * Memory Allocator Functions
 *
 *****************************************************************************/


/**
 * @brief Initialize the allocator over a provided memory block. Creates the global header and the
 * first memory block. Fills the payload with the repeating pattern `0xC0DEF00D55`.
 * 
 * @param heap The base pointer to the heap memory.
 * @param heap_size The number of bytes available in heap.
 * @return int 0 on success, non-zero on failure.
 */
int mm_init(uint8_t *heap, size_t heap_size) {
    if (!heap || heap_size < 256) return -1;


    /* Step 1: detect pattern from heap */
    uint8_t detected[5];
    for (int i = 0; i < 5; i++)
        detected[i] = heap[i];

    /* Step 2: check if heap actually contains a custom pattern */
    int nonzero = 0;
    for (int i = 0; i < 5; i++)
        if (detected[i] != 0)
            nonzero = 1;

    /* Step 3: If nonzero, use the detected pattern */
    if (nonzero) {
        for (int i = 0; i < 5; i++)
            UNUSED_PATTERN[i] = detected[i];
    }


    memset(heap, 0, heap_size);
    g_heap = heap;
    g_heap_size = heap_size;

    GlobalHeader *G = (GlobalHeader*)heap;
    G->consistency = GLOBAL_HEADER_CONSISTENCY;
    G->heap_size = heap_size;

    /* ---- ABSOLUTE pointer alignment ---- */
    uintptr_t base = (uintptr_t)(heap + sizeof(GlobalHeader));
    uintptr_t aligned = ALIGN_UP(base);        // Align REAL MEMORY pointer

    BlockHeader *h = (BlockHeader*)aligned;
    G->first_block = (uint32_t)(aligned - (uintptr_t)heap);

    G->crc = crc32(G, offsetof(GlobalHeader, crc));

    /* Initial block metadata */
    h->consistency = BLOCK_HEADER_CONSISTENCY;
    h->state = FREE;
    h->prev = 0;
    h->next = 0;
    h->seq = 1;

    /* Largest possible payload */
    uint8_t *payload_start = (uint8_t*)h + sizeof(BlockHeader);
    size_t max_payload =
        (size_t)((heap + heap_size) - payload_start - sizeof(BlockFooter));
    h->size = max_payload;

    h->pad_size = compute_padding(h);
    h->crc = get_header_crc(h);

    BlockFooter *f = get_footer(h);
    f->consistency = BLOCK_FOOTER_CONSISTENCY;
    f->size = h->size;
    f->seq = h->seq;
    f->pad_size = h->pad_size;
    f->crc = get_footer_crc(f);

    /* Fill unused memory */
    for (size_t i = 0; i < h->size; i++)
        payload_start[i] = UNUSED_PATTERN[i % 5];

    return 0;
}


/**
 * @brief Allocate a block with ALIGN-byte aligned payload.
 * 
 * @param size The size of the block to be allocated (in bytes).
 * @return void* The aligned payload pointer, or NULL on failure.
 */
void *mm_malloc(size_t size) {
    if (!g_heap || size == 0) return NULL;

    size = ALIGN_UP(size);
    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t off = G->first_block;

    while (off) {
        BlockHeader *h = off_to_ptr(off);
        if (!h) return NULL;

        if (!is_block_valid(h)) {
            quarantine_block(h);
            off = h->next;
            continue;
        }

        if (h->state == FREE && h->size >= size) {
            split_block(h, size);

            h->state = USED;
            h->seq++;

            BlockFooter *f = get_footer(h);
            f->seq = h->seq;
            f->size = h->size;
            f->pad_size = h->pad_size;
            f->crc = get_footer_crc(f);

            h->crc = get_header_crc(h);
            return (uint8_t*)h + sizeof(BlockHeader);
        }

        off = h->next;
    }

    return NULL;
}


/**
 * @brief Safely read data from an allocated block at offset bytes into buf.
 * 
 * @param ptr The pointer to the allocated block.
 * @param offset The offset within the block to read from.
 * @param buf The pointer to the buffer to store read data.
 * @param len The number of bytes to read from the block.
 * @return int The number of bytes read, or -1 if corruption or invalid pointer
 * detected.
 */
int mm_read(void *ptr, size_t offset, void *buf, size_t len) {
    BlockHeader *h = ptr_to_block(ptr);
    if (!h) return -1;

    if (offset + len > h->size) return -1;

    memcpy(buf, ((uint8_t*)h + sizeof(BlockHeader)) + offset, len);
    return (int)len;
}


/**
 * @brief Safely write data into an allocated block at offset bytes from src.
 * 
 * @param ptr The pointer to the allocated block.
 * @param offset The offset within the block to start writing.
 * @param src The pointer to the source data to write.
 * @param len The number of bytes to write.
 * @return int The number of bytes written, or -1 if corruption or invalid
 * pointer detected.
 */
int mm_write(void *ptr, size_t offset, const void *src, size_t len) {
    BlockHeader *h = ptr_to_block(ptr);
    if (!h) return -1;

    if (offset + len > h->size) return -1;

    memcpy(((uint8_t*)h + sizeof(BlockHeader)) + offset, src, len);

    h->seq++;
    h->crc = get_header_crc(h);

    BlockFooter *f = get_footer(h);
    f->seq = h->seq;
    f->pad_size = h->pad_size;
    f->crc = get_footer_crc(f);

    return (int)len;
}


/**
 * @brief Free a previously-allocated block (ignore NULL). Must detect
 * double-free.
 * 
 * @param ptr A pointer to the block to be freed.
 */
void mm_free(void *ptr) {
    if (!ptr) return;

    BlockHeader *h = ptr_to_block(ptr);
    if (!h) return;

    if (h->state != USED) {
        quarantine_block(h);
        return;
    }

    h->state = FREE;
    h->seq++;

    BlockFooter *f = get_footer(h);
    f->seq = h->seq;
    f->pad_size = h->pad_size;
    f->crc = get_footer_crc(f);

    h->crc = get_header_crc(h);

    uint8_t *payload = (uint8_t*)h + sizeof(BlockHeader);
    for (size_t i = 0; i < h->size; i++)
        payload[i] = UNUSED_PATTERN[i % 5];

    merge_free_blocks(h);
}


/**
 * @brief Resize a previously allocated block to new_size bytes, preserving
 * data.
 * 
 * @param ptr The pointer to the block to be resized.
 * @param new_size The new size of the block (in bytes).
 * @return void* The pointer to the resized block, or NULL on failure.
 */
void *mm_realloc(void *ptr, size_t new_size) {
    /* Case 1: Behave like malloc when ptr == NULL */
    if (!ptr)
        return mm_malloc(new_size);

    /* Resolve the block from ptr */
    BlockHeader *h = ptr_to_block(ptr);
    if (!h)
        return NULL; /* ptr invalid or quarantined */

    /* Alignment requirement */
    new_size = ALIGN_UP(new_size);

    /* Case 2: Shrinking or same size → keep block */
    if (new_size <= h->size)
        return ptr;

    /* Case 3: Need larger block → allocate new one */
    void *new_ptr = mm_malloc(new_size);
    if (!new_ptr)
        return NULL;

    /* Copy the old payload safely */
    size_t copy_size = h->size;
    memcpy((uint8_t*)new_ptr,
           (uint8_t*)h + sizeof(BlockHeader),
           copy_size);

    /* Free the old block */
    mm_free(ptr);

    return new_ptr;
}


/******************************************************************************
 *
 * Debug Function
 *
 *****************************************************************************/


/**
 * @brief Output current heap usage and integrity statistics for debugging.
 * 
 */
void mm_heap_stats(void) {
    if (!g_heap) {
        printf("Heap not initialized.\n");
        return;
    }

    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t off = G->first_block;

    printf("=== Heap Stats ===\n");
    printf("Heap size: %u\n", G->heap_size);
    printf("First block offset: %u\n\n", G->first_block);

    while (off) {
        BlockHeader *h = off_to_ptr(off);
        if (!h) {
            printf("Invalid block offset %u\n", off);
            break;
        }

        printf("Block @ offset %u:\n", off);
        printf("    size   = %u\n", h->size);
        printf("    state  = %u (%s)\n",
            h->state,
            (h->state == FREE ? "FREE" :
             (h->state == USED ? "USED" :
              (h->state == QUARANTINED ? "QUARANTINED" : "???"))));
        printf("    prev   = %u\n", h->prev);
        printf("    next   = %u\n", h->next);
        printf("    seq    = %u\n", h->seq);
        printf("    valid  = %d\n", is_block_valid(h));
        printf("\n");

        off = h->next;
    }

    printf("=== End Heap Stats ===\n");
}
