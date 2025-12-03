// Copyright 2025 Rory Poulter
/******************************************************************************
 *
 * Includes and Definitions
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "./allocator.h"


// Pattern for filling unused space in the heap.
const uint8_t UNUSED_PATTERN[5] = { 0xC0, 0xDE, 0xF0, 0x0D, 0x55 };

// Alignment constant and function.
#define ALIGN 40
#define ALIGN_UP(x) (((x) + (ALIGN-1)) / ALIGN * ALIGN)

// Consistency check constants, referred to as `metadata constants`.
#define GLOBAL_HEADER_CONSISTENCY 0xDEADCE11
#define BLOCK_HEADER_CONSISTENCY 0xC0DEBA5E
#define BLOCK_FOOTER_CONSISTENCY 0xBAD1DEA5

// Block states.
#define FREE 0
#define USED 1
#define QUARANTINED 2


/******************************************************************************
 *
 * Data Structures and Related Functions
 *
 *****************************************************************************/

// Global header structure stored at the start of the heap.
typedef struct {
    uint32_t consistency;   // Identifies initialized heap
    uint32_t heap_size;     // Total heap size in bytes
    uint32_t first_block;   // Offset of first block header
    uint32_t crc;           // Cyclic redundancy checksum value
} GlobalHeader;


// Memory block header structure used for error checking and management.
typedef struct {
    uint32_t consistency;   // Basic check for bit flips
    uint32_t size;          // Payload size in bytes
    uint32_t state;         // Free (0), used(1), quarantined (2)
    uint32_t prev;          // Offset of previous block header
    uint32_t next;          // Offset of next block header
    uint32_t seq;           // Sequence number for detecting partial writes
    uint32_t crc;           // Cyclic redundancy checksum value
    uint8_t _pad[12];       // Padding to make 40 bytes
} BlockHeader;


// Memory block footer structure used for error checking and management.
typedef struct {
    uint32_t consistency;   // Basic check for bit flips
    uint32_t size;          // Mirror payload size in bytes
    uint32_t seq;           // Mirror of sequence number
    uint32_t crc;           // Cyclic redundancy checksum value
} BlockFooter;


static uint8_t *g_heap = NULL;       // Base pointer
static size_t   g_heap_size = 0;     // Size of heap memory in bytes

// 1 is debug information should be displayed, 0 if not.
static int debug = 0;


/**
 * @brief Get a pointer to the footer from a pointer of the header and the 
 * block size.
 * 
 * @param h The header pointer.
 * @return * BlockFooter* A pointer to the footer.
 */
static inline BlockFooter* get_footer(BlockHeader *h) {
    if (!h) return NULL;
    return (BlockFooter*)(((uint8_t*)h) + sizeof(BlockHeader) + h->size);
}


/**
 * @brief Calculates the CRC32 value.
 * 
 * @param data The pointer to the data to calculate the CRC32 checksum
 * value for.
 * @param len The length of the data.
 * @return uint32_t The CRC32 checksum value.
 */
uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t*)data;
    // The inital CRC value is set to all 1s.
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        // Read the current byte, increment the pointer and XOR the byte into
        // the CRC.
        crc ^= *p++;
        // Iterate for every bit in the byte.
        for (unsigned k = 0; k < 8; k++)
            /**
             * @note This line performs polynomial division for the CRC32 
             * calculation. It takes the following steps:
             * 1. Determines if the least significant bit (LSB) of the current
             * CRC value is 1 or 0 using `(crc & 1)`.
             * 2. Negates the result to create a mask, either 0x00000000 if
             * the LSB was 0 pr 0xFFFFFFFF if the LSB was 1.
             * 3. ANDs the mask with the reverse polynomial constant
             * `0xEDB88320u` to either apply the polynomial or not.
             * 4. Perform a right bit shift on the current CRC value using
             * `crc >> 1`.
             * 5. XORs the shifted CRC value with the result from step 3 to
             * update the CRC value.
             */
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
    }
    // Invert the bits of the final CRC value before returning.
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
    /* CRC over all bytes up to but NOT including crc field */
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
    /* CRC over all bytes up to but NOT including crc field */
    return crc32(f, offsetof(BlockFooter, crc));
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

    /* Update state first */
    h->state = QUARANTINED;

    /* 1. Increase sequence number */
    h->seq++;

    /* 2. Update footer mirror BEFORE computing CRCs */
    f->seq  = h->seq;
    f->size = h->size;

    /* 3. Write footer CRC */
    f->crc = get_footer_crc(f);

    /* 4. Write header CRC last → atomic update ordering */
    h->crc = get_header_crc(h);
}


/******************************************************************************
 * 
 * Memory Allocation Helper Functions
 * 
 *****************************************************************************/


static inline BlockHeader* place_header_at(uint8_t *start) {
    if (!start) return NULL;

    /* Align the *header* itself to 40 bytes */
    uintptr_t hdr = ALIGN_UP((uintptr_t)start);

    if (hdr + sizeof(BlockHeader) > (uintptr_t)(g_heap + g_heap_size))
        return NULL;

    return (BlockHeader*)hdr;
}


/**
 * @brief Generates an offset corresponding to a pointer.
 * 
 * @param p The pointer.
 * @return uint32_t The offset corresponding to the pointer.
 */
static inline uint32_t ptr_to_off(void *p) {
    // Check if a pointer has been passed.
    if (!p) {
        return 0;
    }
    return (uint32_t)((uint8_t*)p - g_heap);
}


/**
 * @brief Generates a pointer to data located `offset` bytes away.
 * 
 * @param offset The offset of the pointer to be generated.
 * @return void* The pointer corresponding to the offset.
 */
static inline void* off_to_ptr(uint32_t offset) {
    // Check if the offset is 0 or exceeds the heap size.
    if (offset == 0 || offset >= g_heap_size) {
        return NULL;
    }
    return (void*)(g_heap + offset);
}


/**
 * @brief Merges adjacent free valid blocks into one contiguous block.
 * 
 * @param h A pointer to the newly freed block.
 */
void merge_free_blocks(BlockHeader *h) {
    if (!h) return;

    /* Try merging forward repeatedly */
    while (h->next) {
        BlockHeader *h2 = off_to_ptr(h->next);
        if (!h2 || !is_block_valid(h2) || h2->state != FREE)
            break;

        /* Check physical adjacency */
        BlockFooter *f = get_footer(h);
        uint8_t *candidate = (uint8_t*)f + sizeof(BlockFooter);

        /* aligned expected location of the next header */
        BlockHeader *expected = (BlockHeader*)ALIGN_UP((uintptr_t)candidate);

        if (expected != h2)
            break; /* not adjacent → stop, do not risk corruption */

        /***** Perform merge *****/

        /* New size spans both payloads + second header + second footer */
        BlockFooter *f2 = get_footer(h2);
        uint8_t *end_of_h2 = (uint8_t*)f2 + sizeof(BlockFooter);
        uint8_t *payload_start = ((uint8_t*)h + sizeof(BlockHeader));

        size_t merged_payload = (size_t)(end_of_h2 - payload_start -
            sizeof(BlockFooter));
        h->size = merged_payload;

        /* Fix links: skip h2 */
        h->next = h2->next;
        if (h2->next) {
            BlockHeader *h3 = off_to_ptr(h2->next);
            if (is_block_valid(h3)) {
                h3->prev = ptr_to_off(h);
                h3->crc = get_header_crc(h3);

                BlockFooter *f3 = get_footer(h3);
                f3->seq = h3->seq;
                f3->crc = get_footer_crc(f3);
            } else {
                quarantine_block(h3);
            }
        }

        /* Update merged block metadata */
        h->seq++;
        h->crc = get_header_crc(h);

        BlockFooter *new_f = get_footer(h);
        new_f->consistency = BLOCK_FOOTER_CONSISTENCY;
        new_f->size = h->size;
        new_f->seq = h->seq;
        new_f->crc = get_footer_crc(new_f);

        /* Continue merging forward */
    }

    /* Try merging backward once */
    if (h->prev) {
        BlockHeader *p = off_to_ptr(h->prev);
        if (p && is_block_valid(p) && p->state == FREE)
            merge_free_blocks(p);
    }
}


/**
 * @brief Finds the block which a pointer falls within the bounds of.
 * 
 * @param ptr The pointer to identify the block for.
 * @return BlockHeader* A pointer to the header of the block if found, or NULL.
 */
BlockHeader* ptr_to_block(void *ptr) {
    if (!ptr) return NULL;

    uint8_t *p = (uint8_t*)ptr;
    if (p < g_heap || p >= g_heap + g_heap_size)
        return NULL;

    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t off = G->first_block;

    while (off) {
        BlockHeader *h = off_to_ptr(off);
        if (!h) return NULL;

        if (!is_block_valid(h)) {
            quarantine_block(h);
            return NULL;  /* DO NOT FOLLOW A CORRUPTED NEXT POINTER */
        }

        uint8_t *start = ((uint8_t*)h + sizeof(BlockHeader));
        uint8_t *end   = start + h->size;

        if (p >= start && p < end && h->state == USED)
            return h;

        off = h->next;
    }
    return NULL;
}


/**
 * @brief Splits a block into two parts and sets the new partition to `FREE`
 * 
 * @param h A pointer to the block to split
 * @param needed The size of the block to be split
 */
void split_block(BlockHeader *h, size_t needed) {
    if (!h || needed == 0) return;
    if (h->size < needed) return;

    size_t old_payload = h->size;
    size_t leftover = old_payload - needed;

    const size_t MIN_REMAINDER =
        sizeof(BlockHeader) + sizeof(BlockFooter) + ALIGN;

    if (leftover < MIN_REMAINDER)
        return;   /* Not enough space to form a second block */

    /* ---- Shrink original block ---- */
    h->size = needed;
    h->seq++;

    BlockFooter *f1 = get_footer(h);
    f1->consistency = BLOCK_FOOTER_CONSISTENCY;
    f1->size = h->size;
    f1->seq = h->seq;
    f1->crc = get_footer_crc(f1);

    h->crc = get_header_crc(h);

    /* Original block's absolute end */
    uint8_t *orig_footer = ((uint8_t*)h + sizeof(BlockHeader) + old_payload);
    uint8_t *orig_footer_end = orig_footer + sizeof(BlockFooter);

    /* Start searching for new header right after shrunk footer */
    uint8_t *candidate = (uint8_t*)f1 + sizeof(BlockFooter);

    /* Align new header start */
    uintptr_t new_hdr_addr = ALIGN_UP((uintptr_t)candidate);

    uint8_t *nhp = (uint8_t*)new_hdr_addr;

    if (nhp + sizeof(BlockHeader) + sizeof(BlockFooter) > orig_footer_end) {
        /* Can't place second block — revert */
        h->size = old_payload;
        h->seq++;

        BlockFooter *fr = get_footer(h);
        fr->consistency = BLOCK_FOOTER_CONSISTENCY;
        fr->size = h->size;
        fr->seq = h->seq;
        fr->crc = get_footer_crc(fr);

        h->crc = get_header_crc(h);
        return;
    }

    /* new payload size between new header and original footer */
    size_t new_payload = (size_t)(orig_footer_end -
                      (nhp + sizeof(BlockHeader) + sizeof(BlockFooter)));
    if (new_payload < ALIGN) {
        /* revert */
        h->size = old_payload;
        h->seq++;

        BlockFooter *fr = get_footer(h);
        fr->consistency = BLOCK_FOOTER_CONSISTENCY;
        fr->size = h->size;
        fr->seq = h->seq;
        fr->crc = get_footer_crc(fr);

        h->crc = get_header_crc(h);
        return;
    }

    /* ---- Create the new block ---- */
    BlockHeader *nh = (BlockHeader*)nhp;

    nh->consistency = BLOCK_HEADER_CONSISTENCY;
    nh->state = FREE;
    nh->size = new_payload;
    nh->prev = ptr_to_off(h);
    nh->next = h->next;
    nh->seq = 1;
    nh->crc = get_header_crc(nh);

    BlockFooter *f2 = get_footer(nh);
    f2->consistency = BLOCK_FOOTER_CONSISTENCY;
    f2->size = nh->size;
    f2->seq = nh->seq;
    f2->crc = get_footer_crc(f2);

    /* Fix next block if exists */
    if (h->next) {
        BlockHeader *nx = off_to_ptr(h->next);
        if (is_block_valid(nx)) {
            nx->prev = ptr_to_off(nh);
            nx->crc = get_header_crc(nx);
            BlockFooter *nxf = get_footer(nx);
            nxf->seq = nx->seq;
            nxf->crc = get_footer_crc(nxf);
        } else {
            quarantine_block(nx);
        }
    }

    h->next = ptr_to_off(nh);
    h->crc = get_header_crc(h);
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
    printf("Size of header: %lu", sizeof(BlockHeader));
    if (!heap || heap_size < 1024)
        return -1;

    g_heap = heap;
    g_heap_size = heap_size;

    GlobalHeader *G = (GlobalHeader*)g_heap;

    /* If heap already initialized, return success */
    if (G->consistency == GLOBAL_HEADER_CONSISTENCY)
        return 0;

    memset(heap, 0, heap_size);

    /* Initialize global header */
    G->consistency = GLOBAL_HEADER_CONSISTENCY;
    G->heap_size = heap_size;

    /* Compute first header start */
    uint8_t *block_start = g_heap + sizeof(GlobalHeader);
    BlockHeader *h = place_header_at(block_start);
    if (!h) return -1;

    G->first_block = ptr_to_off(h);
    /* CRC covers everything up to crc field */
    G->crc = crc32(G, offsetof(GlobalHeader, crc));

    /* Construct first block header */
    h->consistency = BLOCK_HEADER_CONSISTENCY;
    h->state = FREE;
    h->prev = 0;
    h->next = 0;
    h->seq = 1;

    /* payload_start = header + 40 */
    uint8_t *payload_start = ((uint8_t*)h + sizeof(BlockHeader));

    if (payload_start + sizeof(BlockFooter) > g_heap + g_heap_size)
        return -1;

    /* payload size is everything until final footer */
    h->size = (size_t)((g_heap + g_heap_size) - payload_start -
    sizeof(BlockFooter));

    /* header CRC last */
    h->crc = get_header_crc(h);

    /* footer immediately after payload */
    BlockFooter *f = get_footer(h);
    f->consistency = BLOCK_FOOTER_CONSISTENCY;
    f->size = h->size;
    f->seq = h->seq;
    f->crc = get_footer_crc(f);

    /* fill payload */
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
            return NULL;  /* stop */
        }

        if (h->state == FREE && h->size >= size) {
            split_block(h, size);

            h->state = USED;
            h->seq++;

            BlockFooter *f = get_footer(h);
            f->seq = h->seq;
            f->size = h->size;
            f->crc = get_footer_crc(f);

            h->crc = get_header_crc(h);

            return ((uint8_t*)h + sizeof(BlockHeader));
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

    if (!h)
        return -1;

    if (offset + len > h->size)
        return -1;

    memcpy(buf, (((uint8_t*)h + sizeof(BlockHeader))) + offset, len);
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

    if (!h)
        return -1;

    if (offset + len > h->size)
        return -1;

    memcpy(((uint8_t*)h + sizeof(BlockHeader)) + offset, src, len);

    /* Update block metadata */
    h->seq++;
    h->crc = get_header_crc(h);

    BlockFooter *f = get_footer(h);
    f->seq = h->seq;
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
    f->size = h->size;
    f->crc = get_footer_crc(f);

    h->crc = get_header_crc(h);

    uint8_t *payload = ((uint8_t*)h + sizeof(BlockHeader));
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
    if (!ptr)
        return mm_malloc(new_size);

    BlockHeader *h = ptr_to_block(ptr);
    if (!h)
        return NULL;

    new_size = ALIGN_UP(new_size);

    /* If shrinking or same size → keep in place */
    if (new_size <= h->size)
        return ptr;

    /* Allocate new block */
    void *p2 = mm_malloc(new_size);
    if (!p2)
        return NULL;

    /* Copy old payload */
    mm_write(p2, 0, ((uint8_t*)h + sizeof(BlockHeader)), h->size);

    /* Free old block */
    mm_free(ptr);

    return p2;
}


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
