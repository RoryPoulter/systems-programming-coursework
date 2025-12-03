CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -fPIC
LDFLAGS = -shared

# Source files
RUNME_EXE = runme


# Default target
all: $(LIB) $(RUNME_EXE)


# Build shared library: liballocator.so
allocator.o: allocator.c allocator.h
	$(CC) $(CFLAGS) -c allocator.c -o allocator.o

liballocator.so: allocator.o
	$(CC) $(LDFLAGS) -o liballocator.so allocator.o


# Build runme executable
$(RUNME_EXE): runme.c allocator.h liballocator.so
	$(CC) -Wall -Wextra -Werror runme.c -L. -lallocator -o $(RUNME_EXE)


# "runme" target — runs the runme executable
# (PHONY so it doesn't conflict with the file named runme)
run: $(RUNME_EXE)
	LD_LIBRARY_PATH=. ./runme --seed 1 --storm 0 --size 2048


# Test target — identical or more advanced tests
test: $(RUNME_EXE)
	LD_LIBRARY_PATH=. ./runme --seed 123 --storm 1 --size 4096


# Clean target
clean:
	rm -f allocator.o liballocator.so runme


# Phony targets to avoid conflict with files
.PHONY: all clean test run