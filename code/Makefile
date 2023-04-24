CC=gcc
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse

OBJ=rufs.o block.o

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@ -lm

rufs: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o rufs -lm

.PHONY: clean
clean:
	rm -f *.o rufs

