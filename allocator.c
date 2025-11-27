/**************************************************************************************************
 *
 * Includes and Definitions
 * 
 *************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "allocator.h"


// Pattern for filling unused space in the heap.
const uint8_t UNUSED_PATTERN[5] = { 0xC0, 0xDE, 0xF0, 0x0D, 0x55 };


// Consistency check constants, referred to as `metadata constants`.
#define GLOBAL_HEADER_CONSISTENCY 0xDEADCE11
#define BLOCK_HEADER_CONSISTENCY 0xC0DEBA5E
#define BLOCK_FOOTER_CONSISTENCY 0xBAD1DEA5

// Block states
#define FREE 0
#define USED 1
#define QUARANTINED 2


/**************************************************************************************************
 *
 * Data Structures
 *
 *************************************************************************************************/

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
 * @brief Get a pointer to the footer from a pointer of the header and the block size.
 * 
 * @param h The header pointer.
 * @return * BlockFooter* A pointer to the footer.
 */
BlockFooter* get_footer(BlockHeader *h){
    if (!h){
        return NULL;
    }
    return (BlockFooter*)((uint8_t*)(h + 1) + h->size);
}


/**
 * @brief Calculates the CRC32 value.
 * 
 * @param data The pointer to the data to calculate the CRC32 checksum value for.
 * @param len The length of the data.
 * @return uint32_t The CRC32 checksum value.
 */
uint32_t crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t*)data;
    // The inital CRC value is set to all 1s.
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        // Read the current byte, increment the pointer and XOR the byte into the CRC.
        crc ^= *p++;
        // Iterate for every bit in the byte.
        for (unsigned k = 0; k < 8; k++)
            /**
             * @note This line performs polynomial division for the CRC32 calculation. It takes the following steps:
             * 1. Determines if the least significant bit (LSB) of the current CRC value is 1 or 0 using `(crc & 1)`.
             * 2. Negates the result to create a mask, either 0x00000000 if the LSB was 0 pr 0xFFFFFFFF if the LSB was 1.
             * 3. ANDs the mask with the reverse polynomial constant `0xEDB88320u` to either apply the polynomial or not.
             * 4. Perform a right bit shift on the current CRC value using `crc >> 1`.
             * 5. XORs the shifted CRC value with the result from step 3 to update the CRC value.
             */
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
    }
    // Invert the bits of the final CRC value before returning.
    return crc ^ 0xFFFFFFFFu;
}


/**
 * @brief Calculate the CRC32 checksum using the metadata in a block header excluding the CRC32 value.
 * 
 * @param h A pointer to the block header.
 * @return uint32_t The CRC32 checksum.
 */
uint32_t get_header_crc(const BlockHeader *h){
    return crc32(h, sizeof(BlockHeader) - sizeof(uint32_t));
}


/**
 * @brief Calculate the CRC32 checksum using the metadata in a block footer excluding the CRC32 value.
 * 
 * @param h A pointer to the block footer.
 * @return uint32_t The CRC32 checksum.
 */
uint32_t get_footer_crc(const BlockFooter *f){
    return crc32(f, sizeof(BlockFooter) - sizeof(uint32_t));
}


/**
 * @brief Checks the block metadata for validity.
 * 
 * @param h The pointer to the block header.
 * @return * int A boolean indicating if the block is valid.
 */
int is_block_valid(BlockHeader *h){
    // Check for missing input
    if (!h){
        printf("Block header is NULL.\n");
        return 0;
    }
    
    // Check if header pointer is below that of the heap or beyond the heap bounds.
    if ((uint8_t*)h < g_heap || (uint8_t*)h + sizeof(BlockHeader) > g_heap + g_heap_size){
        printf("Block header is out of heap bounds.\n");
        return 0;
    }

    // Check the header metadata constant.
    if (h->consistency != BLOCK_HEADER_CONSISTENCY){
        printf("Block header consistency check failed.\n");
        return 0;
    }

    // Check the block size does not exceed the heap size.
    if (h->size > g_heap_size){
        printf("Block size exceeds heap size.\n");
        return 0;
    }

    // Check the block header checksum is consistent with the rest of the metadata.
    if (h->crc != get_header_crc(h)){
        printf("Block header checksum inconsistent with header metadata");
        return 0;
    }

    // Retrieve the footer pointer from the header.
    BlockFooter *f = get_footer(h);

    // Check if the footer is beyond the heap bounds.
    if ((uint8_t*)f + sizeof(BlockFooter) > g_heap + g_heap_size)
        return 0;

    // Check the footer metadata constant.
    if (f->consistency != BLOCK_FOOTER_CONSISTENCY){
        printf("Block footer consistency check failed.\n");
        return 0;
    }

    // Check the footer size equals the header size.
    if (f->size != h->size){
        printf("Block footer size is inconsistent with header size.");
        return 0;
    }

    // Check the footer sequence number equals the header sequence number.
    if (f->seq != h->seq){
        printf("Block footer sequence number is inconsistent with header sequence number.\n");
        return 0;
    }

    // Check the block footer checksum is consistent with the rest of the metadata.
    if (f->crc != get_footer_crc(f)){
        printf("Block footer inconsistent with footer size.");
        return 0;
    }

    // If all the checks have passed, the block is valid.
    return 1;
}


/**************************************************************************************************
 * 
 * Memory Allocator Functions
 * 
 *************************************************************************************************/


/**
 * @brief Initialize the allocator over a provided memory block. Creates the global header and the
 * first memory block. Fills the payload with the repeating pattern `0xC0DEF00D55`.
 * 
 * @param heap The base pointer to the heap memory.
 * @param heap_size The number of bytes available in heap.
 * @return int 0 on success, non-zero on failure.
 */
int mm_init(uint8_t *heap, size_t heap_size){
    // Check the argument `heap_size` is large enough.
    if (!heap || heap_size < 1024){
        return -1;
    }
    
    g_heap = heap;
    g_heap_size = heap_size;


    GlobalHeader *G = (GlobalHeader*)g_heap;

    // Detect if the heap already exists.
    if (G->consistency == GLOBAL_HEADER_CONSISTENCY){
        return 0;
    }

    // Initialise a new heap if it does not already exist.
    memset(heap, 0, heap_size);

    // Set the global header metadata.
    G->consistency = GLOBAL_HEADER_CONSISTENCY;
    G->heap_size = heap_size;
    G->first_block = sizeof(GlobalHeader);
    G->crc = crc32(G, sizeof(GlobalHeader)-sizeof(uint32_t));

    // Create one giant free block after the global header.
    BlockHeader *h = (BlockHeader*)(g_heap + G->first_block);
    h->consistency = BLOCK_HEADER_CONSISTENCY;
    h->seq   = 1;
    h->state = FREE;
    h->prev  = 0;
    h->next  = 0;

    // Calculate the payload size
    size_t payload_size = heap_size - sizeof(GlobalHeader) - sizeof(BlockHeader)
    - sizeof(BlockFooter);
    h->size = payload_size;
    h->crc = get_header_crc(h);

    // Build the block footer.
    BlockFooter *f = get_footer(h);
    f->consistency = BLOCK_FOOTER_CONSISTENCY;
    f->size  = h->size;
    f->seq   = h->seq;
    f->crc   = footer_crc(f);

    // Get a pointer to the start of the payload.
    uint8_t *payload = (uint8_t*)(h + 1);
    // Fill the unused space with the repeating pattern.
    for (size_t i=0; i < h->size; i++)
        payload[i] = UNUSED_PATTERN[i % 5];

    return 0;
};


/**
 * @brief Allocate a block with ALIGN-byte aligned payload.
 * 
 * @param size The size of the block to be allocated (in bytes).
 * @return void* The aligned payload pointer, or NULL on failure.
 */
void *mm_malloc(size_t size){};


/**
 * @brief Safely read data from an allocated block at offset bytes into buf.
 * 
 * @param ptr The pointer to the allocated block.
 * @param offset The offset within the block to read from.
 * @param buf The pointer to the buffer to store read data.
 * @param len The number of bytes to read from the block.
 * @return int The number of bytes read, or -1 if corruption or invalid pointer detected.
 */
int mm_read(void *ptr, size_t offset, void *buf, size_t len){};


/**
 * @brief Safely write data into an allocated block at offset bytes from src.
 * 
 * @param ptr The pointer to the allocated block.
 * @param offset The offset within the block to start writing.
 * @param src The pointer to the source data to write.
 * @param len The number of bytes to write.
 * @return int The number of bytes written, or -1 if corruption or invalid pointer detected.
 */
int mm_write(void *ptr, size_t offset, const void *src, size_t len){};


/**
 * @brief Free a previously-allocated pointer (ignore NULL). Must detect double-free.
 * 
 * @param ptr The pointer to be freed.
 */
void mm_free(void *ptr){};


/**
 * @brief Resize a previously allocated block to new_size bytes, preserving data.
 * 
 * @param ptr The pointer to the block to be resized.
 * @param new_size The new size of the block (in bytes).
 * @return void* The pointer to the resized block, or NULL on failure.
 */
void *mm_realloc(void *ptr, size_t new_size){};


/**
 * @brief Output current heap usage and integrity statistics for debugging
 * 
 */
void mm_heap_stats(void){};
