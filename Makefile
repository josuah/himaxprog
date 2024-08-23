CFLAGS = -std=c99 -pedantic -Wall -Wextra -D_XOPEN_SOURCE=900
SRC = himaxprog.c
LIB = -lft4222 -lstdc++

all: himaxprog

clean:
	rm -f himaxprog

himaxprog: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIB)
