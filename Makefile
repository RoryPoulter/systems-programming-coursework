# Compiles all the necessary source files and headers that you submit and creates
# an a library called `allocator.so` and a testing executable called `runme`
all: allocator.c allocator.h
	gcc -o allocator allocator.c allocator.h


# Tests the program with expected data
test:
	./runme

# Cleans the compile environment
clean:
	rm -f allocator.so runme
