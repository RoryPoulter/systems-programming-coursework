#include <stddef.h>
#include <stdint.h>


void mm_init(uint8_t *heap, size_t heap_size);
void *mm_malloc(size_t size);
int mm_read(void *ptr, size_t offset, void *buf, size_t len);
int mm_write(void *ptr, size_t offset, const void *src, size_t len);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t new_size);
void mm_heap_stats(void);
