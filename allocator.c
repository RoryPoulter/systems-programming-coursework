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


/**
 * @brief Get a pointer to the footer from a pointer of the header and the 
 * block size.
 * 
 * @param h The header pointer.
 * @return * BlockFooter* A pointer to the footer.
 */
BlockFooter* get_footer(BlockHeader *h) {
    if (!h) {
        return NULL;
    }
    return (BlockFooter*)((uint8_t*)(h + 1) + h->size);
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
    return crc32(h, sizeof(BlockHeader) - sizeof(uint32_t));
}


/**
 * @brief Calculate the CRC32 checksum using the metadata in a block footer
 * excluding the CRC32 value.
 * 
 * @param h A pointer to the block footer.
 * @return uint32_t The CRC32 checksum.
 */
uint32_t get_footer_crc(const BlockFooter *f) {
    return crc32(f, sizeof(BlockFooter) - sizeof(uint32_t));
}


/**
 * @brief Checks the block metadata for validity.
 * 
 * @param h The pointer to the block header.
 * @return * int A boolean indicating if the block is valid.
 */
int is_block_valid(BlockHeader *h) {
    if (!h) {
        return 0;
    }

    // Check if header pointer is within the heap bounds.
    if ((uint8_t*)h < g_heap || (uint8_t*)h + sizeof(BlockHeader) >
    g_heap + g_heap_size) {
        return 0;
    }

    // Check the header metadata constant.
    if (h->consistency != BLOCK_HEADER_CONSISTENCY) {
        return 0;
    }

    // Check the block size does not exceed the heap size.
    if (h->size > g_heap_size) {
        return 0;
    }

    // Check the block header checksum is consistent.
    if (h->crc != get_header_crc(h)) {
        return 0;
    }

    // Retrieve the footer pointer from the header.
    BlockFooter *f = get_footer(h);

    // Check if the footer is beyond the heap bounds.
    if ((uint8_t*)f + sizeof(BlockFooter) > g_heap + g_heap_size) {
        return 0;
    }

    // Check the footer metadata constant.
    if (f->consistency != BLOCK_FOOTER_CONSISTENCY) {
        // printf("[X]\n");
        return 0;
    }

    // Check the footer size equals the header size.
    if (f->size != h->size) {
        return 0;
    }

    // Check the footer sequence number equals the header sequence number.
    if (f->seq != h->seq) {
        return 0;
    }

    // Check the block footer checksum is consistent.
    if (f->crc != get_footer_crc(f)) {
        return 0;
    }

    // If all the checks have passed, the block is valid.
    return 1;
}


/**
 * @brief Updates the metadata of a block when it has been corrupted so the
 * memory allocator will quarantine it.
 * 
 * @param h A pointer to the header of the corrupted block.
 */
void quarantine_block(BlockHeader *h) {
    // Check for missing input
    if (!h) {
        return;
    }
    // Update header metadata
    h->state = QUARANTINED;
    h->seq++;
    h->crc = get_header_crc(h);
    // Update footer metadata
    BlockFooter *f = get_footer(h);
    f->seq = h->seq;
    f->crc = get_footer_crc(f);
}


/******************************************************************************
 * 
 * Memory Allocation Helper Functions
 * 
 *****************************************************************************/


static inline BlockHeader* place_header_at(uint8_t *start) {
    if (!start) return NULL;
    /* Compute a payload address that is ALIGN-aligned and sits after 
    start + sizeof(BlockHeader).
    We compute aligned_payload = ALIGN_UP(start + sizeof(BlockHeader)),
    then header pointer = aligned_payload - sizeof(BlockHeader).
    This guarantees (header+1) == aligned_payload and is aligned.
    */
    uintptr_t aligned_payload = ALIGN_UP((uintptr_t)(start +
        sizeof(BlockHeader)));
    BlockHeader *h = (BlockHeader*)(aligned_payload - sizeof(BlockHeader));
    /* Safety: ensure header is not before start */
    if ((uint8_t*)h < start) return NULL;
    return h;
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
    if (!h) {
        return;
    }
    /* Try to merge forward while possible */
    while (h->next) {
        BlockHeader *h2 = off_to_ptr(h->next);
        if (!h2) break;
        if (!is_block_valid(h2) || h2->state != FREE) break;

        /* Ensure blocks are physically adjacent in memory: the header of h2
        must be the same as the address we would expect (right after h's footer,
        accounting for any padding used to align h2). */
        BlockFooter *f_h = get_footer(h);
        uint8_t *candidate = (uint8_t*)f_h + sizeof(BlockFooter);
        BlockHeader *expected_h2 = place_header_at(candidate);

        if (expected_h2 != h2) {
            /* They are not contiguous as expected; don't merge to avoid
            swallowing unrelated memory. Quarantine h2 (defensive) and stop
            merging. */
            quarantine_block(h2);
            break;
        }

        /* Compute merged payload size using addresses to include any padding
        between payload start and footer: merged_payload = (end_of_h2_footer) -
        (h payload start) - sizeof(BlockFooter) */
        uint8_t *payload_start = (uint8_t*)(h + 1);
        BlockFooter *f_h2 = get_footer(h2);
        uint8_t *end_of_h2_footer = (uint8_t*)f_h2 + sizeof(BlockFooter);
        if (end_of_h2_footer <= payload_start + sizeof(BlockFooter)) {
            /* Defensive check: sizes would underflow or be invalid — 
            quarantine and stop. */
            quarantine_block(h2);
            break;
        }
        size_t merged_payload = (size_t)(end_of_h2_footer - payload_start -
            sizeof(BlockFooter));

        /* Update links: h will absorb h2 */
        h->size = merged_payload;
        h->next = h2->next;

        /* If there's a block after h2, update its prev pointer */
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

        /* Update merged header/footer metadata */
        h->seq++;
        h->crc = get_header_crc(h);

        BlockFooter *f_new = get_footer(h);
        f_new->consistency = BLOCK_FOOTER_CONSISTENCY;
        f_new->size = h->size;
        f_new->seq = h->seq;
        f_new->crc = get_footer_crc(f_new);

        /* Continue loop to try to merge further forward (coalesce chain) */
    }
    /**
     * @note Recursive call: if the previous block is free and valid, the
     * function would will be called recursively with the previous block. This
     * will merge it with the recently merged block. This repeats until a block
     * is found that cannot be merged.
     */
    if (h->prev) {
        BlockHeader *p = off_to_ptr(h->prev);
        if (is_block_valid(p) && p->state == FREE) {
            merge_free_blocks(p);
        }
    }
}


/**
 * @brief Finds the block which a pointer falls within the bounds of.
 * 
 * @param ptr The pointer to identify the block for.
 * @return BlockHeader* A pointer to the header of the block if found, or NULL.
 */
BlockHeader* ptr_to_block(void* ptr) {
    // Check for missing input.
    if (!ptr) {
        return NULL;
    }
    uint8_t *p = ptr;
    // Check for out of bounds pointer.
    if (p < g_heap || p >= g_heap + g_heap_size) {
        return NULL;
    }
    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t off = G->first_block;

    // Traverses the linked list.
    while (off) {
        // Get the header of the current block from the offset.
        BlockHeader *h = off_to_ptr(off);
        // If the block is invalid, quarantine and move on.
        if (!is_block_valid(h)) {
            quarantine_block(h);
            off = h->next;
            continue;
        }
        // Initialise pointers for start and end of payload.
        uint8_t *start = (uint8_t*)(h+1);
        uint8_t *end = start + h->size;
        // NEW: recompute actual payload start after header alignment
        // Check if the pointer falls within the payload.
        if (p >= start && p < end && h->state == USED)
            return h;
        off = h->next;
    }
    // If the pointer lies within the heap, but in a free or quarantined block.
    return NULL;
}


/**
 * @brief Splits a block into two parts and sets the new partition to `FREE`
 * 
 * @param h A pointer to the block to split
 * @param needed The size of the block to be split
 */
void split_block(BlockHeader* h, size_t needed) {
    if (!h || !needed) {
        return;
    }

    // Check the block can fit the needed size
    // if (leftover < sizeof(BlockHeader)+sizeof(BlockFooter)+ALIGN){
    //     return;
    // }
    if (h->size < needed) return;
    size_t leftover = h->size - needed;

    // Update the old block metadata
    h->size = needed;
    h->seq++;
    h->crc = get_header_crc(h);

    BlockFooter *f = get_footer(h);
    f->size = h->size;
    f->consistency = BLOCK_FOOTER_CONSISTENCY;
    f->size = h->size;
    f->seq = h->seq;
    f->crc = get_footer_crc(f);

    /* Candidate start location for the new header is right after the footer */
    uint8_t *candidate = (uint8_t*)f + sizeof(BlockFooter);

    /* Place the new header so that (new_h+1) is ALIGN aligned */
    BlockHeader *new_h = place_header_at(candidate);
    if (!new_h) {
        /* cannot place aligned header — revert size change (safety) */
        /* revert: better to mark as no split rather than leave inconsistent */
        /* We revert header to old size (previously needed+leftover) */
        /* old size = needed + leftover */
        h->size = needed + leftover;
        h->seq++;
        h->crc = get_header_crc(h);
        f = get_footer(h);
        f->size = h->size;
        f->seq = h->seq;
        f->crc = get_footer_crc(f);
        return;
    }

    /* padding between candidate and actual new header placement */
    size_t padding = (size_t)((uint8_t*)new_h - candidate);

    /* Compute the new payload size for new_h accounting for padding and the
       space taken by the new header+footer. leftover originally included all
       bytes after the shrunk 'needed' payload; we must subtract the padding and
       the metadata sizes to get the new payload. */
    if (leftover < padding + sizeof(BlockHeader) + sizeof(BlockFooter)) {
        /* Not enough space to create a second block after alignment; cancel */
        /* revert to original size */
        h->size = needed + leftover;
        h->seq++;
        h->crc = get_header_crc(h);
        f = get_footer(h);
        f->size = h->size;
        f->seq = h->seq;
        f->crc = get_footer_crc(f);
        return;
    }

    size_t new_payload = leftover - padding - sizeof(BlockHeader) -
    sizeof(BlockFooter);
    if (new_payload < ALIGN) {
        /* Keep a minimum payload (ALIGN) for any split block to avoid tiny
        fragments */
        h->size = needed + leftover;
        h->seq++;
        h->crc = get_header_crc(h);
        f = get_footer(h);
        f->size = h->size;
        f->seq = h->seq;
        f->crc = get_footer_crc(f);
        return;
    }

    /* Initialize new block metadata */
    new_h->consistency = BLOCK_HEADER_CONSISTENCY;
    new_h->seq = 1;
    new_h->state = FREE;
    new_h->prev = ptr_to_off(h);
    new_h->next = h->next;
    new_h->size = new_payload;
    new_h->crc = get_header_crc(new_h);

    /* New footer */
    BlockFooter *new_f = get_footer(new_h);
    new_f->consistency = BLOCK_FOOTER_CONSISTENCY;
    new_f->size = new_h->size;
    new_f->seq = new_h->seq;
    new_f->crc = get_footer_crc(new_f);

    // If the old block had a next block
    if (h->next) {
        BlockHeader *next_h = off_to_ptr(h->next);
        if (is_block_valid(next_h)) {
            next_h->prev = ptr_to_off(new_h);
            next_h->crc = get_header_crc(next_h);
            BlockFooter *next_f = get_footer(next_h);
            next_f->seq = next_h->seq;
            next_f->crc = get_footer_crc(next_f);
        } else {
            quarantine_block(next_h);
        }
    }

    // Set the `next` block to the new block
    h->next = ptr_to_off(new_h);
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
    // Check the argument `heap_size` is large enough.
    if (!heap || heap_size < 1024) {
        return -1;
    }

    g_heap = heap;
    g_heap_size = heap_size;


    GlobalHeader *G = (GlobalHeader*)g_heap;

    // Detect if the heap already exists.
    if (G->consistency == GLOBAL_HEADER_CONSISTENCY) {
        return 0;
    }

    // Initialise a new heap if it does not already exist.
    memset(heap, 0, heap_size);

    // Set the global header metadata.
    G->consistency = GLOBAL_HEADER_CONSISTENCY;
    G->heap_size = heap_size;
    //=========================================================================
    // G->first_block = sizeof(GlobalHeader);
    uint8_t *start_for_first = g_heap + sizeof(GlobalHeader);
    BlockHeader *h = place_header_at(start_for_first);
    if (!h) return -1;
    G->first_block = ptr_to_off(h);
    G->crc = crc32(G, sizeof(GlobalHeader) - sizeof(uint32_t));
    //=========================================================================

    // Create one giant free block after the global header.
    //================================================
    // BlockHeader *h = (BlockHeader*)(g_heap + G->first_block);
    //================================================
    h->consistency = BLOCK_HEADER_CONSISTENCY;
    h->seq = 1;
    h->state = FREE;
    h->prev = 0;
    h->next = 0;

    /* Compute payload size as bytes between payload start and footer space
       before the end of the heap. This correctly accounts for header placement
       alignment and any padding inserted. */
    uint8_t *payload_start = (uint8_t*)(h + 1);
    /* Footer must fit after payload, so we leave sizeof(BlockFooter) bytes */
    if ((size_t)(g_heap + g_heap_size - payload_start) < sizeof(BlockFooter)) {
        return -1;
    }
    size_t payload_size = (size_t)(g_heap + g_heap_size - payload_start -
        sizeof(BlockFooter));

    // Calculate the payload size
    // size_t payload_size = heap_size -sizeof(GlobalHeader)-sizeof(BlockHeader)
    // - sizeof(BlockFooter);
    h->size = payload_size;
    h->crc = get_header_crc(h);

    // Build the block footer.
    BlockFooter *f = get_footer(h);
    f->consistency = BLOCK_FOOTER_CONSISTENCY;
    f->size = h->size;
    f->seq = h->seq;
    f->crc = get_footer_crc(f);

    // Get a pointer to the start of the payload.
    uint8_t *payload = (uint8_t*)(h + 1);
    // Fill the unused space with the repeating pattern.
    for (size_t i=0; i < h->size; i++) {
        payload[i] = UNUSED_PATTERN[i % 5];
    }
    return 0;
}


/**
 * @brief Allocate a block with ALIGN-byte aligned payload.
 * 
 * @param size The size of the block to be allocated (in bytes).
 * @return void* The aligned payload pointer, or NULL on failure.
 */
void *mm_malloc(size_t size) {
// If there is no heap or the size is zero, fail safely.
    if (!g_heap || size == 0) {
        return NULL;
    }
    // Align the size to the 40-byte alignment.
    size = ALIGN_UP(size);
    // Get the offset of the first block.
    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t off = G->first_block;

    while (off) {
        BlockHeader *h = off_to_ptr(off);
        if (!h) {
            return NULL;
        }
        // If the block is invalid, quarantine and move on.
        if (!is_block_valid(h)) {
            quarantine_block(h);
            off = h->next;
            continue;
        }
        // If the block is free and not smaller than `size`, allocate.
        if (h->state == FREE && h->size >= size) {
            // Split the current block to a partition of the correct size.
            split_block(h, size);
            // Update the block metadata.
            h->state = USED;
            h->seq++;
            BlockFooter *f = get_footer(h);
            f->seq = h->seq;
            f->size = h->size;
            f->crc = get_footer_crc(f);
            h->crc = get_header_crc(h);
            // Return the pointer to the block.
            return (void*)(h+1);
        }
        off = h->next;
    }
    // No blocks are free, fail safely.
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
    // If `h` is a NULL pointer or if the data to be read exceeds the payload.
    if (!h || offset+len > h->size) {
        return -1;
    }
    //? No need to validate the block as this is implied by reaching here.
    // Copy the data from the block payload.
    memcpy(buf, (uint8_t*)(h+1)+offset, len);
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
    // If `h` is a NULL pointer or if the data to be read exceeds the payload.
    if (!h || offset+len > h->size) {
        return -1;
    }
    //? No need to validate the block as this is implied by reaching here.
    memcpy((uint8_t*)(h+1)+offset, src, len);
    // Update the block metadata.
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
    // Check for missing input.
    if (!ptr) {
        return;
    }
    BlockHeader *h = ptr_to_block(ptr);
    if (!h) {
        return;
    }
    // Check if the block is free or quarantined. Prevents double-free.
    if (h->state != USED) {
        /* Remove from linked list if possible to prevent later traversal */
        if (h->prev) {
            BlockHeader *p = off_to_ptr(h->prev);
            if (is_block_valid(p)) {
                p->next = h->next;
                p->crc = get_header_crc(p);
                BlockFooter *pf = get_footer(p);
                pf->seq = p->seq;
                pf->crc = get_footer_crc(pf);
            }
        }
        if (h->next) {
            BlockHeader *n = off_to_ptr(h->next);
            if (is_block_valid(n)) {
                n->prev = h->prev;
                n->crc = get_header_crc(n);
                BlockFooter *nf = get_footer(n);
                nf->seq = n->seq;
                nf->crc = get_footer_crc(nf);
            }
        }
        h->prev = h->next = 0;

        quarantine_block(h);
        return;
    }
    // Update block metadata.
    h->state = FREE;
    h->seq++;
    BlockFooter *f = get_footer(h);
    f->seq = h->seq;
    f->size = h->size;
    f->crc = get_footer_crc(f);
    h->crc = get_header_crc(h);
    // Refill payload with repeating pattern.
    uint8_t *payload = (uint8_t*)(h+1);
    for (size_t i = 0; i < h->size; i++) {
        payload[i] = UNUSED_PATTERN[i%5];
    }
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
    // If there is no pointer, allocate a block of size `new_size`.
    if (!ptr) {
        return mm_malloc(new_size);
    }
    BlockHeader *h = ptr_to_block(ptr);
    if (!h) {
        return NULL;
    }
    // Align the size to the 40-byte alignment.
    new_size = ALIGN_UP(new_size);
    // If the new size is smaller than the existing block, return the pointer.
    if (new_size <= h->size) {
        return ptr;
    }

    // Allocate a new larger block.
    void *p2 = mm_malloc(new_size);
    if (!p2) {
        return NULL;
    }
    // Copy the old data and free the old block.
    mm_write(p2, 0, (uint8_t*)(h+1), h->size);
    mm_free(ptr);
    return p2;
}


/**
 * @brief Output current heap usage and integrity statistics for debugging.
 * 
 */
void mm_heap_stats(void) {
    // Get a pointer to the global header
    GlobalHeader *G = (GlobalHeader*)g_heap;
    // The offset to the first block to begin the traversal
    uint32_t off = G->first_block;

    // Traverse the heap and print the status of each block.
    while (off) {
        BlockHeader *h = off_to_ptr(off);
        printf("Block stats:\n"
            "    Offset = %u\n"
            "    Size = %u\n"
            "    State = %u\n"
            "    Valid = %d\n", off, h->size, h->state, is_block_valid(h));
        off = h->next;
    }
}
