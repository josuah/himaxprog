CFLAGS = -std=c99 -pedantic -Wall -Wextra -D_XOPEN_SOURCE=900
SRC = himaxprog.c
LIB = -lft4222 -lstdc++
PREFIX = /usr/local

all: himaxprog

clean:
	rm -f himaxprog

himaxprog: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LIB)

install: himaxprog
	cp himaxprog $(PREFIX)/bin/himaxprog
