CC = gcc
#? -fPIC flag from here: 
#? https://stackoverflow.com/questions/26689759/
#? makefile-for-creating-so-file-from-existing-files
CFLAGS = -Wall -Wextra -Werror -fPIC
LDFLAGS = -shared

# Default target
all: liballocator.so runme

# Build shared library: liballocator.so
allocator.o: allocator.c allocator.h
	$(CC) $(CFLAGS) -c allocator.c -o allocator.o

liballocator.so: allocator.o
	$(CC) $(LDFLAGS) -o liballocator.so allocator.o

runme.o: runme.c
	$(CC) $(CFLAGS) -c runme.c -o runme.o

# Build runme executable
runme: runme.o allocator.h liballocator.so
	$(CC) -o runme runme.o -L. -lallocator -Wl,-rpath,'./'

# Test target — identical or more advanced tests
test: runme
	LD_LIBRARY_PATH=. ./runme --seed 123 --storm 1 --size 4096

# Clean target
clean:
	rm -f allocator.o runme.o liballocator.so runme

# Phony targets to avoid conflict with files
.PHONY: all clean test