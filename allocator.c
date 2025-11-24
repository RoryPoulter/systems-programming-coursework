#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


// Initialize the allocator over a provided memory block.
// Returns 0 on success, non-zero on failure.
int mm_init(uint8_t *heap, size_t heap_size){};


// Allocate a block with ALIGN-byte aligned payload. Returns
// NULL on failure.
void *mm_malloc(size_t size){};


// Safely read data from an allocated block at offset bytes into buf.
// Returns the number of bytes read, or -1 if corruption or invalid pointer detected.
int mm_read(void *ptr, size_t offset, void *buf, size_t len){};


// Safely write data into an allocated block at offset bytes from src.
// Returns the number of bytes written, or -1 if corruption or invalid pointer detected.
int mm_write(void *ptr, size_t offset, const void *src, size_t len){};


// Free a previously-allocated pointer (ignore NULL).
// Must detect double-free.
void mm_free(void *ptr){};


// Resize a previously allocated block to new_size bytes,
// preserving data. [See additional credit]
void *mm_realloc(void *ptr, size_t new_size){};


// Output current heap usage and integrity statistics
// for debugging (No Credit, helper function).
void mm_heap_stats(void){};
