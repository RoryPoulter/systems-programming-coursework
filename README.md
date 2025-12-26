# Robust Dynamic Memory Allocation

![GitHub Release](https://img.shields.io/github/v/release/RoryPoulter/systems-programming-coursework?style=for-the-badge)
![GitHub Issues or Pull Requests](https://img.shields.io/github/issues/RoryPoulter/systems-programming-coursework?style=for-the-badge)
![GitHub License](https://img.shields.io/github/license/RoryPoulter/systems-programming-coursework?style=for-the-badge)
![GitHub last commit](https://img.shields.io/github/last-commit/RoryPoulter/systems-programming-coursework?style=for-the-badge)


## Contents
- [Robust Dynamic Memory Allocation](#robust-dynamic-memory-allocation)
	- [Contents](#contents)
	- [Design](#design)
		- [Example Layout](#example-layout)
	- [Usage](#usage)
		- [Compilation](#compilation)
		- [Using the Allocator](#using-the-allocator)
			- [Initialising the heap with `mm_init`](#initialising-the-heap-with-mm_init)
				- [Parameters](#parameters)
				- [Returns](#returns)
				- [Example](#example)
			- [Allocating memory with `mm_malloc`](#allocating-memory-with-mm_malloc)
				- [Parameters](#parameters-1)
				- [Returns](#returns-1)
				- [Example](#example-1)
			- [Reallocating memory with `mm_realloc`](#reallocating-memory-with-mm_realloc)
				- [Parameters](#parameters-2)
				- [Returns](#returns-2)
				- [Example](#example-2)
			- [Freeing memory with `mm_free`](#freeing-memory-with-mm_free)
				- [Parameters](#parameters-3)
				- [Example](#example-3)
			- [Reading from the allocator with `mm_read`](#reading-from-the-allocator-with-mm_read)
				- [Parameters](#parameters-4)
				- [Returns](#returns-3)
				- [Example](#example-4)
			- [Writing to the allocator with `mm_write`](#writing-to-the-allocator-with-mm_write)
				- [Parameters](#parameters-5)
				- [Returns](#returns-4)
				- [Example](#example-5)
			- [Debugging with `mm_heap_stats`](#debugging-with-mm_heap_stats)
				- [Example](#example-6)
				- [Example Output](#example-output)



## Design

The heap is composed of 3 parts:
* **The global header**:
  * This stores metadata about the entire heap:
    * A constant value `0xDEADCE11`
    * The size of the heap in bytes
    * The offset to the first block in bytes
    * A checksum of the above data
  * The total size of the global header is 16 bytes
* **Padding**:
  * Unused space to align the memory blocks to 40-byte increments
* **The memory blocks**:
  * These store the actual data, and are comprised of a further 3 parts:
    * **The payload**:
      * This stores the data and is the size passed to `mm_malloc` or `mm_realloc`
    * **The block header**:
      * This stores metadata about the block:
        * A constant value `0xC0DEBA5E`
        * The payload size in bytes
        * The state of the block (free, used, quarantined)
        * Offsets to the next and previous block in bytes, or 0 if there is none
        * A counter of the operations performed on the block
        * A checksum on the above values
        * A checksum on the payload data
        * The size of the padding after the footer
        * Padding
      * The total size of the header is 40 bytes
    * **The block footer**:
      * This stores more metadata:
        * A constant value `0xBAD1DEA5`
        * Mirrored size and counter values
        * A checksum on the above values
        * Mirrored padding size
        * Padding
      * The total size of the footer is 20 bytes

The checksum algorithm used is *Fletcher-32*.

### Example Layout

The below diagram is the result of calling the following functions:
```c
// Initialise a heap of the chosen size 400 bytes
size_t heap_size = 400;
uint8_t heap[heap_size];
// Initialise the memory allocator
mm_init(heap, heap_size);
// Allocate a block with a payload of 224 bytes
void *p = mm_malloc(224);
```

![Allocator Layout](design/design.drawio.png)

## Usage

### Compilation

To compile the code, run the command:
```cmd
make all
```
This will produce the following files:
* `runme`
* `liballocator.so`
* `runme.o`
* `allocator.o`

To remove the compiled files, run the command:
```cmd
make clean
```

### Using the Allocator

#### Initialising the heap with `mm_init`

```c
int mm_init(uint8_t *heap, size_t heap_size)
```
Initialises the heap for the memory allocator.

##### Parameters
* `heap` - An array pointer to become the heap
* `heap_size` - The size of the heap in bytes

##### Returns
* 0 on success, non-zero on failure

##### Example

```c
uint8_t heap_size = 1024;  // Set the heap size
uint8_t heap[heap_size];   // Create an array of the desired size
mm_init(heap, heap_size);  // Initialise the heap
```

#### Allocating memory with `mm_malloc`

```c
void *mm_malloc(size_t size)
```
Allocates a 40-byte aligned block of memory in the heap.

##### Parameters
* `size` - The size of the payload of the memory block in the heap

##### Returns
* A pointer to the payload of the allocated block, or `NULL` on failure

##### Example

```c
void *p = mm_malloc(128);  // Allocate a block with payload of 128 bytes
void *p_2 = mm_malloc(256);  // Allocate a block with payload of 256 bytes
```

#### Reallocating memory with `mm_realloc`

```c
void *realloc(void *ptr, size_t new_size)
```
Allocates a block of the new size and frees the block the pointer points to.

##### Parameters
* `ptr` - A pointer to the block to be reallocated
  * If no pointer is passed, `mm_malloc(new_size)` is called and no block is freed
* `new_size` - The size of the new block in the heap

##### Returns
* A pointer to the newly allocated block

##### Example

```c
void *new_p = mm_realloc(p, 256);  // Reallocate the block at pointer p
```

#### Freeing memory with `mm_free`

```c
void mm_free(void *ptr)
```
Frees the block at the specified pointer and fills the payload with the repeating pattern.

##### Parameters
* `ptr` - A pointer to the block to be freed

##### Example

```c
mm_free(new_ptr);  // Free the block at new_ptr
```

#### Reading from the allocator with `mm_read`

```c
int mm_read(void *ptr, size_t offset, void *buf, size_t len)
```
Copies data from the block payload to a buffer and returns the number of read bytes.

##### Parameters

* `ptr` - A pointer to the block to be read from
* `offset` - The offset from the start of the payload to start reading from
* `buf` - A pointer to the buffer to copy the data to
* `len` - The length of the payload segment to read

##### Returns

* The number of bytes read, or -1 if corruption or invalid pointer detected

##### Example

```c
uint8_t buf[256];  // Initialise the buffer
// Check if the number of bytes read is correct
if (mm_read(p_2, 0, buf, 256) != 256) {
	fprintf(stderr, "mm_read failed\n");
    return 1;
}
// Output the contents of the buffer
for (int i = 0; i < 256; i++) {
	printf("Byte %d: 0x%02x\n", i, buf[i])
}
```

#### Writing to the allocator with `mm_write`

```c
int mm_write(void *ptr, size_t offset, const void *src, size_t len)
```
Copies data from a buffer to the block payload and returns the number of written bytes.

##### Parameters

* `ptr` - A pointer to the block to be written to
* `offset` - The offset from the start of the payload to start writing to
* `buf` - A pointer to the buffer to copy the data from
* `len` - The length of the payload segment to written

##### Returns

* The number of bytes written, or -1 if corruption or invalid pointer detected

##### Example

```c
uint8_t buf[128];  // Initialises the buffer
// Fill the buffer with data, e.g. 0 - 127
for (int i = 0; i < 128; i++) {
	buf[i] = (uint8_t)i;
}
// Check if the number of bytes written is correct
if (mm_write(p, 0, buf, 128) != 128) {
	fprintf(stderr, "mm_write detected corruption\n");
	return 1;
}
```

#### Debugging with `mm_heap_stats`

```c
void mm_heap_stats(void)
```

Outputs metadata about the blocks in the heap.

##### Example

```c
size_t heap_size = 400;
uint8_t heap[heap_size];
// Initialise the memory allocator
mm_init(heap, heap_size);
// Allocate a block with payload size 128 bytes
void *p = mm_malloc(128);
// Output info about the heap
mm_heap_stats();
```

##### Example Output
```
=== Heap Stats ===
Heap size: 400
First block offset: 40

Block @ 0x5d059f225068 (offset @ 40):
  Header:
    consistency = c0deba5e
    size        = 128
    state       = 1 (USED)
    prev        = 0
    next        = 240
    seq         = 3
    checksum         = 3507401084
    payload checksum = 0
  Footer:
    consistency = bad1dea5
    size        = 128
    seq         = 3
    checksum         = 31521433
    valid       = 1

Block @ 0x5d059f225130 (offset @ 240):
  Header:
    consistency = c0deba5e
    size        = 100
    state       = 0 (FREE)
    prev        = 40
    next        = 0
    seq         = 1
    checksum         = 3708471931
    payload checksum = 0
  Footer:
    consistency = bad1dea5
    size        = 100
    seq         = 1
    checksum         = 2380258457
    valid       = 1

=== End Heap Stats ===
```