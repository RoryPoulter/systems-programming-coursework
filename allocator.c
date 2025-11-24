#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "allocator.h"


/**
 * @brief Initialize the allocator over a provided memory block.
 * 
 * @param heap The base pointer to the heap memory.
 * @param heap_size The number of bytes available in heap.
 * @return int 0 on success, non-zero on failure.
 */
int mm_init(uint8_t *heap, size_t heap_size){
    if (!heap || heap_size < 1024)  /* minimum size sanity check */
        return -1;
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
