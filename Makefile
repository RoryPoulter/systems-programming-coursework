# Compiles all the necessary source files and headers that you submit and creates
# an a library called `allocator.so` and a testing executable called `runme`
all: allocator.c allocator.h
# 	Source - https://stackoverflow.com/questions/14884126/build-so-file-from-c-file-using-gcc-command-line
# 	Posted by dreamcrash, modified by community. See post 'Timeline' for change history
# 	Retrieved 2025-11-24, License - CC BY-SA 3.0
	gcc -shared -o liballocator.so -fPIC allocator.c allocator.h


# Tests the program with expected data
test:
	./runme

# Cleans the compile environment
clean:
	rm -f allocator.so runme


runme:
	gcc -o runme runme.c -L. -liballocator