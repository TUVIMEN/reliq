SHELL	=	/bin/bash
CC	=	gcc -std=c11
LINK	=
CFLAGS	=	-O2 -mavx2 -pipe -march=native -Wall -Wextra
TARGET	=	hgrep

OBJECTS = $(patsubst %.c, %.o, $(wildcard src/*.c))

all: $(OBJECTS)
	$(CC) $(LINK) $(CFLAGS) $^ -o $(TARGET)
	strip --discard-all $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install:
	cp -f ${TARGET} /usr/bin

uninstall:
	rm /usr/bin/${TARGET}

clean:
	find . -name "*.o" -exec rm "{}" \;
	rm $(TARGET)
