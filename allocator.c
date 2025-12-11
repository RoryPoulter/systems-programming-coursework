// Copyright 2025 Rory Poulter
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

// 1 if debug information should be displayed, 0 if not.
#define debug 0

// ----- GLOBAL HEADER -----
typedef struct {
    uint32_t consistency;       // Metadata constant (0xDEADCE11)
    uint32_t heap_size;         // Total heap size
    uint32_t first_block;       // Offset to the first block
    uint32_t checksum;          // Checksum over above data ^
} GlobalHeader;

// ----- BLOCK HEADER -----
typedef struct {
    uint32_t consistency;       // Metadata constant (0xC0DEBA5E)
    uint32_t size;              // Payload size
    uint32_t state;             // Block state (FREE, USED, QUARANTINED)
    uint32_t prev;              // Offset to previous block, or 0 if none
    uint32_t next;              // Offset to next block, or 0 if none
    uint32_t seq;               // Counter for number of operations performed
    uint32_t checksum;          // Checksum over above data ^
    uint32_t payload_checksum;  // Checksum over payload data
    uint8_t  pad_size;          // Padding size after the footer
    uint8_t  _pad[7];           // 7-byte padding to align payload pointers
} BlockHeader;

// ----- BLOCK FOOTER -----
typedef struct {
    uint32_t consistency;       // Metadata constant (0xBAD1DEA5)
    uint32_t size;              // Mirrored payload size
    uint32_t seq;               // Mirrored counter
    uint32_t checksum;          // Checksum over above data ^
    uint8_t  pad_size;          // NEW: padding mirrored
    uint8_t  _pad[3];           // 3-byte padding to make 20 bytes
} BlockFooter;

// Globals
static uint8_t *g_heap = NULL;
static size_t g_heap_size = 0;


/**
 * @brief Calculates the checksum using the fletcher-32 algorithm.
 * 
 * @param data The pointer to the data to calculate the checksum value for.
 * @param len The length of the data.
 * @return uint32_t The checksum value.
 */
uint32_t fletcher_32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;

    uint32_t sum1 = 0xffffu;
    uint32_t sum2 = 0xffffu;

    // Process input in 16-bit words
    while (len > 1) {
        uint16_t word = (uint16_t)(p[0] << 8 | p[1]);
        sum1 = (sum1 + word) % 0xffffu;
        sum2 = (sum2 + sum1) % 0xffffu;
        p += 2;
        len -= 2;
    }

    // Handle odd byte (pad with zero)
    if (len == 1) {
        uint16_t word = (uint16_t)(p[0] << 8);
        sum1 = (sum1 + word) % 0xffffu;
        sum2 = (sum2 + sum1) % 0xffffu;
    }

    return (sum2 << 16) | sum1;
}


/**
 * @brief Calculate the checksum using the metadata in a block header
 * excluding the checksum value.
 * 
 * @param header_ptr A pointer to the block header.
 * @return uint32_t The checksum for the header.
 */
uint32_t get_header_checksum(const BlockHeader *header_ptr) {
    return fletcher_32(header_ptr, offsetof(BlockHeader, checksum));
}


/**
 * @brief Calculate the checksum using the metadata in a block footer
 * excluding the checksum value.
 * 
 * @param footer_ptr A pointer to the block footer.
 * @return uint32_t The checksum for the footer.
 */
uint32_t get_footer_checksum(const BlockFooter *footer_ptr) {
    return fletcher_32(footer_ptr, offsetof(BlockFooter, checksum));
}


/**
 * @brief calculate the checksum using the payload contents.
 * 
 * @param header_ptr A pointer to the block header.
 * @return uint32_t The checksum for the payload.
 */
uint32_t get_payload_checksum(const BlockHeader *header_ptr) {
    return fletcher_32((void *)header_ptr + 40, header_ptr->size);
}


/**
 * @brief Generates an offset corresponding to a pointer.
 * 
 * @param ptr The pointer.
 * @return uint32_t The offset corresponding to the pointer.
 */
static inline uint32_t ptr_to_offset(void *ptr) {
    if (!ptr) return 0;
    return (uint32_t)((uint8_t*)ptr - g_heap);
}


/**
 * @brief Generates a pointer to data located `offset` bytes away.
 * 
 * @param offset The offset of the pointer to be generated.
 * @return void* The pointer corresponding to the offset.
 */
static inline void *offset_to_ptr(uint32_t offset) {
    if (offset == 0 || offset >= g_heap_size) return NULL;
    return g_heap + offset;
}


/**
 * @brief Get a pointer to the footer from a pointer of the header and the 
 * block size.
 * 
 * @param header_ptr The header pointer.
 * @return * BlockFooter* A pointer to the footer.
 */
static inline BlockFooter *get_footer_ptr(BlockHeader *header_ptr) {
    return (BlockFooter*)((uint8_t*)header_ptr + sizeof(BlockHeader) +
    header_ptr->size);
}


/**
 * @brief Checks the block metadata for validity.
 * 
 * @param header_ptr The pointer to the block header.
 * @return * int A boolean indicating if the block is valid.
 */
int is_block_valid(BlockHeader *header_ptr) {
    // Input sanitisation
    if (!header_ptr) {
        if (debug) printf("No header passed.\n");
        return 0;
    }

    // Check if header pointer is within the heap bounds.
    if ((uint8_t*)header_ptr < g_heap || (uint8_t*)header_ptr +
    sizeof(BlockHeader) > g_heap + g_heap_size) {
        if (debug) printf("Header is out of heap bounds.\n");
        return 0;
    }

    // Check the header metadata constant.
    if (header_ptr->consistency != BLOCK_HEADER_CONSISTENCY) {
        if (debug) printf("Header metadata constant is invalid.\n");
        return 0;
    }

    // Check the block size does not exceed the heap size.
    if (header_ptr->size > g_heap_size) {
        if (debug) printf("Block size exceeds heap size.\n");
        return 0;
    }

    // Check the block header checksum is consistent.
    if (header_ptr->checksum != get_header_checksum(header_ptr)) {
        if (debug) printf("Header checksum not valid.\n");
        return 0;
    }

    // Check the payload checksum is consistent.
    if (header_ptr->payload_checksum != get_payload_checksum(header_ptr)) {
        if (debug) printf("Payload checksum not valid.\n");
        return 0;
    }

    // Retrieve the footer pointer from the header.
    BlockFooter *footer_ptr = get_footer_ptr(header_ptr);

    // Check if the footer is beyond the heap bounds.
    if ((uint8_t*)footer_ptr + sizeof(BlockFooter) > g_heap + g_heap_size) {
        if (debug) printf("Footer is out of heap bounds.\n");
        return 0;
    }

    // Check the footer metadata constant.
    if (footer_ptr->consistency != BLOCK_FOOTER_CONSISTENCY) {
        if (debug) printf("Footer metadata constant is invalid.\n");
        return 0;
    }

    // Check the footer size equals the header size.
    if (footer_ptr->size != header_ptr->size) {
        if (debug) printf("Footer size does not match header.\n");
        return 0;
    }

    // Check the footer sequence number equals the header sequence number.
    if (footer_ptr->seq != header_ptr->seq) {
        if (debug) printf("Footer sequence does not match header.\n");
        return 0;
    }

    // Check the block footer checksum is consistent.
    if (footer_ptr->checksum != get_footer_checksum(footer_ptr)) {
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
 * @param header_ptr A pointer to the header of the corrupted block.
 */
void quarantine_block(BlockHeader *header_ptr) {
    // If not pointer is passed.
    if (!header_ptr) return;
    if (debug) printf("Quarantining block at %p.\n", header_ptr);
    BlockFooter *footer_ptr = get_footer_ptr(header_ptr);

    // Update the block metadata.
    header_ptr->state = QUARANTINED;
    header_ptr->seq++;

    footer_ptr->seq = header_ptr->seq;
    footer_ptr->size = header_ptr->size;
    footer_ptr->pad_size = header_ptr->pad_size;

    footer_ptr->checksum = get_footer_checksum(footer_ptr);
    header_ptr->checksum = get_header_checksum(header_ptr);
}


/**
 * @brief Splits a block into two parts and sets the new partition to `FREE`.
 * 
 * @param header_ptr A pointer to the block to split.
 * @param needed The size of the new memory partition.
 */
static void split_block(BlockHeader *header_ptr, size_t needed) {
    // Store the original payload size.
    size_t original_size = header_ptr->size;

    // Minimum viable block size.
    size_t min_block_size = sizeof(BlockHeader) + 1 + sizeof(BlockFooter) +
    ALIGN;

    // If there is not enough room to split the block.
    if (original_size - needed < min_block_size) {
        if (debug) printf("Not enough room to split.\n");
        return;
    }

    // Update block A metadata.
    uint8_t *payload_a_ptr = (uint8_t *)header_ptr + sizeof(BlockHeader);
    header_ptr->size = needed;
    BlockFooter *footer_a_ptr = (BlockFooter *)(payload_a_ptr +
        header_ptr->size);

    size_t footer_a_end = (uint8_t *)footer_a_ptr - g_heap +
    sizeof(BlockFooter);
    size_t footer_a_pad = (ALIGN - (footer_a_end % ALIGN)) % ALIGN;

    header_ptr->pad_size = footer_a_pad;
    header_ptr->seq++;
    header_ptr->checksum = get_header_checksum(header_ptr);

    // Create a new footer for block A.
    footer_a_ptr->consistency = BLOCK_FOOTER_CONSISTENCY;
    footer_a_ptr->size = header_ptr->size;
    footer_a_ptr->seq = header_ptr->seq;
    footer_a_ptr->pad_size = footer_a_pad;
    footer_a_ptr->checksum = get_footer_checksum(footer_a_ptr);

    // Add the padding to align block B.
    uint8_t *pad_a_ptr = (uint8_t *)footer_a_ptr + sizeof(BlockFooter);
    for (size_t i = 0; i < footer_a_pad; i++)
        pad_a_ptr[i] = UNUSED_PATTERN[i % 5];

    // Create block B header.
    BlockHeader *header_b_ptr = (BlockHeader *)(pad_a_ptr + footer_a_pad);

    // Calculate the size of block B.
    uint8_t *start_a = (uint8_t *)header_ptr;
    uint8_t *start_b = (uint8_t *)header_b_ptr;
    size_t bytes_used_by_a = start_b - start_a;
    size_t block_b_size = original_size - bytes_used_by_a;

    // Populate block B header metadata.
    header_b_ptr->consistency = BLOCK_HEADER_CONSISTENCY;
    header_b_ptr->state = FREE;
    header_b_ptr->size = block_b_size;
    header_b_ptr->pad_size = 0;
    header_b_ptr->prev = (uint32_t)(start_a - g_heap);
    header_b_ptr->next = header_ptr->next;
    header_b_ptr->seq = 1;
    header_b_ptr->checksum = get_header_checksum(header_b_ptr);
    header_b_ptr->payload_checksum = get_payload_checksum(header_b_ptr);

    // Fix the linked list.
    header_ptr->next = (uint8_t *)header_b_ptr - g_heap;
    header_ptr->checksum = get_header_checksum(header_ptr);

    if (header_b_ptr->next) {
        BlockHeader *hn = (BlockHeader *)(g_heap + header_b_ptr->next);
        hn->prev = (uint8_t *)header_b_ptr - g_heap;
        hn->checksum = get_header_checksum(hn);
    }

    // Create a footer for block B and populate metadata.
    uint8_t *block_b_payload = (uint8_t *)header_b_ptr + sizeof(BlockHeader);
    BlockFooter *footer_b_ptr = (BlockFooter *)(block_b_payload +
        header_b_ptr->size);

    footer_b_ptr->consistency = BLOCK_FOOTER_CONSISTENCY;
    footer_b_ptr->size = header_b_ptr->size;
    footer_b_ptr->seq = header_b_ptr->seq;
    footer_b_ptr->pad_size = 0;  // no pad yet
    footer_b_ptr->checksum = get_footer_checksum(footer_b_ptr);
}


/**
 * @brief Merges adjacent free valid blocks into one contiguous block.
 * 
 * @param header_ptr A pointer to the newly freed block.
 */
static void merge_free_blocks(BlockHeader *header_ptr) {
    while (1) {
        // If there are no following blocks.
        if (!header_ptr->next) {
            if (debug) printf("No next block from %p.\n", header_ptr);
            return;
        }
        BlockHeader *header_b_ptr = offset_to_ptr(header_ptr->next);

        // If h->next does not point to a block.
        if (!header_b_ptr) {
            if (debug) printf("h->next does not point to a block.\n");
            return;
        }

        // If the next block is not free.
        if (header_b_ptr->state != FREE) {
            if (debug) printf("Block at %p is not free.\n", header_b_ptr);
            return;
        }

        // Calculate merged payload size.
        uint8_t *block_a_payload_ptr = (uint8_t *)header_ptr +
        sizeof(BlockHeader);

        BlockFooter *footer_b_ptr = get_footer_ptr(header_b_ptr);

        uint8_t *block_b_end_ptr = (uint8_t *)footer_b_ptr +
        sizeof(BlockFooter) + footer_b_ptr->pad_size;

        size_t new_size = block_b_end_ptr - block_a_payload_ptr -
        sizeof(BlockFooter);

        header_ptr->size = new_size;

        // Calculate the new padding.
        size_t footer_end = (block_a_payload_ptr - g_heap)
                          + header_ptr->size
                          + sizeof(BlockFooter);

        size_t pad = (ALIGN - (footer_end % ALIGN)) % ALIGN;

        header_ptr->pad_size = pad;
        header_ptr->seq++;
        header_ptr->checksum = get_header_checksum(header_ptr);

        // Write a new footer for the merged block.
        BlockFooter *footer_a_ptr = (BlockFooter *)(block_a_payload_ptr +
            header_ptr->size);

        footer_a_ptr->consistency = BLOCK_FOOTER_CONSISTENCY;
        footer_a_ptr->size = header_ptr->size;
        footer_a_ptr->seq = header_ptr->seq;
        footer_a_ptr->pad_size = pad;
        footer_a_ptr->checksum = get_footer_checksum(footer_a_ptr);

        uint8_t *pad_ptr = (uint8_t *)footer_a_ptr + sizeof(BlockFooter);
        for (size_t i = 0; i < pad; i++)
            pad_ptr[i] = UNUSED_PATTERN[i % 5];

        // Fix the link list.
        header_ptr->next = header_b_ptr->next;
        header_ptr->checksum = get_header_checksum(header_ptr);
        header_ptr->payload_checksum = get_payload_checksum(header_ptr);

        // If there is a next block, update the linked list backwards.
        if (header_b_ptr->next) {
            BlockHeader *hn = (BlockHeader *)(g_heap + header_b_ptr->next);
            hn->prev = (uint8_t *)header_ptr - g_heap;
            hn->checksum = get_header_checksum(hn);
        }
        // If there is a previous block.
        if (header_ptr->prev) {
            BlockHeader *prev_header_ptr = offset_to_ptr(header_ptr->prev);
            // If the block is free and valid, recurse with it.
            if (is_block_valid(prev_header_ptr) &&
            prev_header_ptr->state == FREE) {
                merge_free_blocks(prev_header_ptr);
            }
        } else {
            if (debug) printf("No previous block.\n");
        }
    }
}


/**
 * @brief Finds the block which a pointer falls within the bounds of.
 * 
 * @param ptr The pointer to identify the block for.
 * @return BlockHeader* A pointer to the header of the block if found, or NULL.
 */
BlockHeader *ptr_to_block(void *ptr) {
    if (!ptr) {
        if (debug) printf("No pointer provided.\n");
        return NULL;
    }

    uint8_t *p = ptr;
    if (p < g_heap || p >= g_heap + g_heap_size) {
        if (debug) printf("Pointer outside of heap bounds.");
        return NULL;
    }
    GlobalHeader *G = (GlobalHeader*)g_heap;
    uint32_t offset = G->first_block;

    // Traverse the list until a block containing the pointer is found.
    while (offset) {
        BlockHeader *header_ptr = offset_to_ptr(offset);
        if (!header_ptr) {
            if (debug) printf("No blocks in heap.\n");
            return NULL;
        }

        if (!is_block_valid(header_ptr)) {
            if (debug) printf("Block at %p invalid, quarantining.\n",
                header_ptr);
            quarantine_block(header_ptr);
            offset = header_ptr->next;
            continue;
        }

        uint8_t *payload_start_ptr = (uint8_t*)header_ptr + sizeof(BlockHeader);
        uint8_t *payload_end_ptr = payload_start_ptr + header_ptr->size;
        if (p >= payload_start_ptr && p < payload_end_ptr &&
            header_ptr->state == USED) {
            if (debug) printf("Pointer falls within block at %p.\n",
                header_ptr);
            return header_ptr;
        }
        offset = header_ptr->next;
    }
    if (debug) printf("Pointer %p falls within no blocks.\n", ptr);
    return NULL;
}


/**
 * @brief Initialize the allocator over a provided memory block. Creates the global header and the
 * first memory block. Fills the payload with the repeating pattern `0xC0DEF00D55`.
 * 
 * @param heap The base pointer to the heap memory.
 * @param heap_size The number of bytes available in heap.
 * @return int 0 on success, non-zero on failure.
 */
int mm_init(uint8_t *heap, size_t heap_size) {
    // if (!heap || heap_size < 512)
    //     return -1;

    g_heap = heap;
    g_heap_size = heap_size;

    // Detect unused pattern
    uint8_t detected[5];
    memcpy(detected, heap, 5);

    int nonzero = 0;
    for (int i = 0; i < 5; i++)
        if (detected[i] != 0) nonzero = 1;

    if (nonzero)
        memcpy(UNUSED_PATTERN, detected, 5);

    memset(heap, 0, heap_size);

    // Create the GlobalHeader.
    GlobalHeader *global_header_ptr = (GlobalHeader *)heap;
    global_header_ptr->consistency = GLOBAL_HEADER_CONSISTENCY;
    global_header_ptr->heap_size = heap_size;

    // Calculate aligned offset for first block.
    uintptr_t raw_addr = (uintptr_t)(heap + sizeof(GlobalHeader));
    uint32_t raw_offset = (uint32_t)(raw_addr - (uintptr_t)heap);

    uint32_t aligned_offset = ALIGN_UP(raw_offset);

    BlockHeader *header_ptr = (BlockHeader *)(heap + aligned_offset);
    uint32_t first_block_offset = aligned_offset;

    global_header_ptr->first_block = first_block_offset;
    global_header_ptr->checksum = fletcher_32((BlockHeader *)global_header_ptr,
                    sizeof(GlobalHeader) - sizeof(uint32_t));

    // Calculate maximum payload size.
    uint8_t *payload_ptr = (uint8_t *)header_ptr + sizeof(BlockHeader);

    size_t usable = heap_size - first_block_offset - sizeof(BlockHeader) -
    sizeof(BlockFooter);

    // Calculate the padding for the next block to align it.
    size_t footer_end = first_block_offset + sizeof(BlockHeader) + usable +
    sizeof(BlockFooter);

    size_t pad = (ALIGN - (footer_end % ALIGN)) % ALIGN;
    usable -= pad;

    // Populate the block header metadata.
    header_ptr->consistency = BLOCK_HEADER_CONSISTENCY;
    header_ptr->state = FREE;
    header_ptr->size = usable;
    header_ptr->pad_size = pad;
    header_ptr->prev = 0;
    header_ptr->next = 0;
    header_ptr->seq = 1;
    header_ptr->checksum = get_header_checksum(header_ptr);

    // Create the block footer and populate metadata.
    BlockFooter *footer_ptr = (BlockFooter *)(payload_ptr + header_ptr->size);

    footer_ptr->consistency = BLOCK_FOOTER_CONSISTENCY;
    footer_ptr->size = header_ptr->size;
    footer_ptr->seq = header_ptr->seq;
    footer_ptr->pad_size = pad;
    footer_ptr->checksum = get_footer_checksum(footer_ptr);

    // Write the unused pattern.
    uint8_t *pad_ptr = (uint8_t *)footer_ptr + sizeof(BlockFooter);
    for (size_t i = 0; i < pad; i++)
        pad_ptr[i] = UNUSED_PATTERN[i % 5];
    header_ptr->payload_checksum = get_payload_checksum(header_ptr);
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

    GlobalHeader *global_header_ptr = (GlobalHeader*)g_heap;
    uint32_t offset = global_header_ptr->first_block;

    while (offset) {
        BlockHeader *header_ptr = offset_to_ptr(offset);
        if (!header_ptr) return NULL;

        if (!is_block_valid(header_ptr)) {
            quarantine_block(header_ptr);
            offset = header_ptr->next;
            continue;
        }

        if (header_ptr->state == FREE && header_ptr->size >= size) {
            split_block(header_ptr, size);

            header_ptr->state = USED;
            header_ptr->seq++;
            header_ptr->payload_checksum = get_payload_checksum(header_ptr);

            BlockFooter *footer_ptr = get_footer_ptr(header_ptr);
            footer_ptr->seq = header_ptr->seq;
            footer_ptr->size = header_ptr->size;
            footer_ptr->pad_size = header_ptr->pad_size;
            footer_ptr->checksum = get_footer_checksum(footer_ptr);

            header_ptr->checksum = get_header_checksum(header_ptr);
            return (uint8_t*)header_ptr + sizeof(BlockHeader);
        }
        offset = header_ptr->next;
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
    BlockHeader *header_ptr = ptr_to_block(ptr);
    if (!header_ptr) return -1;

    if (!is_block_valid(header_ptr)) {
        if (debug) printf("Quarantining block at %p", header_ptr);
        quarantine_block(header_ptr);
        return -1;
    }
    if (offset + len > header_ptr->size) return -1;
    memcpy(buf, ((uint8_t*)header_ptr + sizeof(BlockHeader)) + offset, len);
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
    BlockHeader *header_ptr = ptr_to_block(ptr);
    if (!header_ptr) return -1;

    if (!is_block_valid(header_ptr)) {
        if (debug) printf("Quarantining block at %p", header_ptr);
        quarantine_block(header_ptr);
        return -1;
    }

    if (offset + len > header_ptr->size) return -1;

    memcpy(((uint8_t*)header_ptr + sizeof(BlockHeader)) + offset, src, len);

    header_ptr->seq++;
    header_ptr->checksum = get_header_checksum(header_ptr);
    header_ptr->payload_checksum = get_payload_checksum(header_ptr);

    BlockFooter *footer_ptr = get_footer_ptr(header_ptr);
    footer_ptr->seq = header_ptr->seq;
    footer_ptr->pad_size = header_ptr->pad_size;
    footer_ptr->checksum = get_footer_checksum(footer_ptr);

    return (int)len;
}


/**
 * @brief Free a previously-allocated block (ignore NULL). Must detect
 * double-free.
 * 
 * @param ptr A pointer to the block to be freed.
 */
void mm_free(void *ptr) {
    printf("mm_free(%p)\n", ptr);
    if (!ptr) {
        if (debug) printf("    No pointer.\n");
        return;
    }
    if (debug) printf("    Freeing block at %p.\n", ptr);

    BlockHeader *header_ptr = ptr_to_block(ptr);
    if (!header_ptr) {
        if (debug) printf("    No header to pointer.\n");
        return;
    }
    if (debug) printf("    Header pointer = %p.\n", header_ptr);

    if (header_ptr->state != USED) {
        if (debug) printf("    Block is not used, quarantining.\n");
        quarantine_block(header_ptr);
        return;
    }

    if (!is_block_valid(header_ptr)) {
        if (debug) printf("Quarantining block at %p", header_ptr);
        quarantine_block(header_ptr);
        return;
    }

    header_ptr->state = FREE;
    header_ptr->seq++;

    BlockFooter *footer_ptr = get_footer_ptr(header_ptr);
    footer_ptr->seq = header_ptr->seq;
    footer_ptr->pad_size = header_ptr->pad_size;
    footer_ptr->checksum = get_footer_checksum(footer_ptr);

    header_ptr->checksum = get_header_checksum(header_ptr);

    uint8_t *payload_ptr = (uint8_t*)header_ptr + sizeof(BlockHeader);
    for (size_t i = 0; i < header_ptr->size; i++)
        payload_ptr[i] = UNUSED_PATTERN[i % 5];
    header_ptr->payload_checksum = get_payload_checksum(header_ptr);

    if (debug) printf("    Mergeing free blocks...\n");
    merge_free_blocks(header_ptr);
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
    // If not pointer provided, behave like mm_malloc.
    if (!ptr) return mm_malloc(new_size);

    // Get the header pointer for the block.
    BlockHeader *header_ptr = ptr_to_block(ptr);
    if (!header_ptr) return NULL;

    if (!is_block_valid(header_ptr)) {
        if (debug) printf("Quarantining block at %p", header_ptr);
        quarantine_block(header_ptr);
        return NULL;
    }
    // If they are the same size, return the pointer.
    if (new_size == header_ptr->size) return ptr;

    // If you need a larger block, allocate a new one and free the old.
    void *new_ptr = mm_malloc(new_size);
    if (!new_ptr) return NULL;

    // Copy the old payload safely
    size_t copy_size = header_ptr->size;
    memcpy((uint8_t*)new_ptr, (uint8_t*)header_ptr + sizeof(BlockHeader),
    copy_size);

    // Free the old block
    mm_free(ptr);
    return new_ptr;
}


/**
 * @brief Prints the block metadata.
 * 
 * @param header_ptr A pointer to the block header.
 */
void print_block_stats(BlockHeader *header_ptr) {
    BlockFooter *f = get_footer_ptr(header_ptr);
    printf("Block @ %p (offset @ %u):\n",
        header_ptr, ptr_to_offset(header_ptr));
    printf("  Header:\n");
    printf("    consistency = %x\n", header_ptr->consistency);
    printf("    size        = %u\n", header_ptr->size);
    printf("    state       = %u (%s)\n",
        header_ptr->state,
        (header_ptr->state == FREE ? "FREE" :
            (header_ptr->state == USED ? "USED" :
                (header_ptr->state == QUARANTINED ? "QUARANTINED" : "???"))));
    printf("    prev        = %u\n", header_ptr->prev);
    printf("    next        = %u\n", header_ptr->next);
    printf("    seq         = %u\n", header_ptr->seq);
    printf("    checksum         = %u\n", header_ptr->checksum);
    printf("    payload checksum = %u\n", header_ptr->payload_checksum);
    printf("  Footer:\n");
    printf("    consistency = %x\n", f->consistency);
    printf("    size        = %u\n", f->size);
    printf("    seq         = %u\n", f->seq);
    printf("    checksum         = %u\n", f->checksum);
    printf("    valid       = %d\n\n", is_block_valid(header_ptr));
}


/**
 * @brief Output current heap usage and integrity statistics for debugging.
 * 
 */
void mm_heap_stats() {
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
        BlockHeader *h = offset_to_ptr(off);
        if (!h) {
            printf("Invalid block offset %u\n", off);
            break;
        }
        print_block_stats(h);
        off = h->next;
    }
    printf("=== End Heap Stats ===\n");
}
