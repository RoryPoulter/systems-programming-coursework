#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


/**
 * @brief Initialize the allocator over a provided memory block.
 * 
 * @param heap 
 * @param heap_size 
 * @return int 0 on success, non-zero on failure.
 */
int mm_init(uint8_t *heap, size_t heap_size){};


/**
 * @brief Allocate a block with ALIGN-byte aligned payload.
 * 
 * @param size 
 * @return void* Returns NULL on failure.
 */
void *mm_malloc(size_t size){};


/**
 * @brief Safely read data from an allocated block at offset bytes into buf.
 * 
 * @param ptr 
 * @param offset 
 * @param buf 
 * @param len 
 * @return int The number of bytes read, or -1 if corruption or invalid pointer detected.
 */
int mm_read(void *ptr, size_t offset, void *buf, size_t len){};


/**
 * @brief Safely write data into an allocated block at offset bytes from src.
 * 
 * @param ptr 
 * @param offset 
 * @param src 
 * @param len 
 * @return int The number of bytes written, or -1 if corruption or invalid pointer detected.
 */
int mm_write(void *ptr, size_t offset, const void *src, size_t len){};


/**
 * @brief Free a previously-allocated pointer (ignore NULL). Must detect double-free.
 * 
 * @param ptr 
 */
void mm_free(void *ptr){};


/**
 * @brief Resize a previously allocated block to new_size bytes, preserving data.
 * 
 * @param ptr 
 * @param new_size 
 * @return void* 
 */
void *mm_realloc(void *ptr, size_t new_size){};


/**
 * @brief Output current heap usage and integrity statistics for debugging
 * 
 */
void mm_heap_stats(void){};
