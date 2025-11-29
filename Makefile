CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -fPIC
LDFLAGS = -shared

# Source files
ALLOC_SRC = allocator.c
ALLOC_OBJ = allocator.o
LIB       = liballocator.so

RUNME_SRC = runme.c
RUNME_EXE = runme


# Default target
all: $(LIB) $(RUNME_EXE)


# Build shared library: liballocator.so
$(ALLOC_OBJ): $(ALLOC_SRC) allocator.h
	$(CC) $(CFLAGS) -c $(ALLOC_SRC) -o $(ALLOC_OBJ)

$(LIB): $(ALLOC_OBJ)
	$(CC) $(LDFLAGS) -o $(LIB) $(ALLOC_OBJ)


# Build runme executable
$(RUNME_EXE): $(RUNME_SRC) allocator.h $(LIB)
	$(CC) -Wall -Wextra -Werror $(RUNME_SRC) -L. -lallocator -o $(RUNME_EXE)


# "runme" target — runs the runme executable
# (PHONY so it doesn't conflict with the file named runme)
run: $(RUNME_EXE)
	LD_LIBRARY_PATH=. ./runme --seed 1 --storm 0 --size 2048


# Test target — identical or more advanced tests
test: $(RUNME_EXE)
	LD_LIBRARY_PATH=. ./runme --seed 123 --storm 1 --size 4096


# Clean target
clean:
	rm -f $(ALLOC_OBJ) $(LIB) $(RUNME_EXE)


# Phony targets to avoid conflict with files
.PHONY: all clean test run