SHELL	=	/bin/bash
CC	=	gcc -std=c11
LINK	=
CFLAGS	=	-O2 -pipe -march=native -Wall -Wextra
TARGET	=	hgrep

OBJECTS = $(patsubst %.c, %.o, $(wildcard src/*.c))

all: $(OBJECTS)
	$(CC) $(LINK) $(CFLAGS) $^ -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	find . -name "*.o" -exec rm "{}" \;
	rm $(TARGET)
